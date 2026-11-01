#include "database.h"

#include "db_path.h"
#include "db_stmt.h"
#include "db_util.h"
#include "migrations.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

std::wstring parent_directory(const std::wstring& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    return parent.wstring();
}

std::wstring join_path(const std::wstring& directory, const std::wstring& name) {
    return (std::filesystem::path(directory) / name).wstring();
}

// CSV: поле всегда в кавычках, внутренние кавычки удваиваются.
std::string csv_field(const std::wstring& value) {
    std::string text = ws2u(value);
    std::string escaped;
    escaped.reserve(text.size() + 2);
    escaped.push_back('"');
    for (char c : text) {
        if (c == '"') escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

Database::Database() = default;
Database::~Database() { close(); }

Database::Transaction::Transaction(Database& db)
    : db_(db), lock_(db.write_mutex_), active_(false) {
    if (db_.transaction_depth_ > 0) {
        throw DbError("Вложенные транзакции запрещены");
    }
    exec_sql(db_.db_, "BEGIN IMMEDIATE");
    db_.transaction_depth_ = 1;
    active_ = true;
}

Database::Transaction::~Transaction() {
    if (active_) db_.rollback_transaction();
}

void Database::Transaction::commit() {
    if (!active_) return;
    db_.commit_transaction();
    active_ = false;
}

void Database::commit_transaction() {
    try {
        exec_sql(db_, "COMMIT");
    } catch (...) {
        transaction_depth_ = 0;
        throw;
    }
    transaction_depth_ = 0;
}

void Database::rollback_transaction() {
    transaction_depth_ = 0;
    if (!db_) return;
    try {
        exec_sql(db_, "ROLLBACK");
    } catch (...) {
        // Откат после ошибки: соединение может быть уже вне транзакции.
    }
}

bool Database::open(const std::wstring& path, const DatabaseOptions& options) {
    close();
    last_error_.clear();

    std::wstring error;
    if (!db_path_is_valid(path, error)) {
        last_error_ = error;
        return false;
    }

    options_ = options;
    const std::wstring directory = parent_directory(path);
    if (!db_ensure_directory(directory, error)) {
        last_error_ = error;
        return false;
    }
    if (options_.backup_dir.empty()) options_.backup_dir = join_path(directory, L"backup");

    if (options_.require_secure_acl && !db_dir_acl_is_restricted(directory, error)) {
        last_error_ = error;
        return false;
    }

    const std::string path_utf8 = ws2u(path);
    const int rc = sqlite3_open_v2(path_utf8.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = L"Не удалось открыть базу данных";
        if (db_) {
            last_error_ += L": " + u2ws(sqlite3_errmsg(db_));
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        return false;
    }
    path_ = path;

    try {
        pool_ = std::make_unique<StatementPool>(db_);
        // Репозитории создаются до миграции: миграция пишет запись в аудит.
        tasks_ = std::make_unique<TaskRepository>(*pool_);
        windows_ = std::make_unique<TimeWindowRepository>(*pool_);
        history_ = std::make_unique<HistoryRepository>(*pool_);
        settings_ = std::make_unique<SettingsRepository>(*pool_);
        audit_ = std::make_unique<AuditRepository>(*pool_);
        trigger_state_ = std::make_unique<TriggerStateRepository>(*pool_);

        exec_sql(db_, "PRAGMA busy_timeout=5000");
        exec_sql(db_, "PRAGMA journal_mode=WAL");
        exec_sql(db_, "PRAGMA synchronous=NORMAL");
        exec_sql(db_, "PRAGMA foreign_keys=ON");
        exec_sql(db_, "PRAGMA wal_autocheckpoint=1000");

        if (!quick_check()) {
            close();
            return false;
        }
        if (!migrate()) {
            close();
            return false;
        }

        // Retention при старте (DB.md, раздел 6). Ошибки не блокируют открытие.
        try {
            purge_expired_audit();
            purge_expired_outputs();
        } catch (...) {
            // Очистка — обслуживающая операция; отказ фиксируется следующей записью.
        }
    } catch (const std::exception& e) {
        last_error_ = u2ws(e.what());
        close();
        return false;
    }
    return true;
}

void Database::close() {
    if (!db_) return;
    if (pool_) pool_->clear();
    sqlite3_exec(db_, "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr);
    sqlite3_close_v2(db_);
    db_ = nullptr;
    pool_.reset();
    tasks_.reset();
    windows_.reset();
    history_.reset();
    settings_.reset();
    audit_.reset();
    trigger_state_.reset();
}

int Database::schema_version() const {
    if (!db_) return 0;
    return query_int(db_, "PRAGMA user_version");
}

bool Database::quick_check() {
    try {
        const std::string result = query_text(db_, "PRAGMA quick_check");
        if (result != "ok") {
            last_error_ = L"Проверка целостности БД не пройдена: " + u2ws(result);
            return false;
        }
    } catch (const std::exception& e) {
        last_error_ = u2ws(e.what());
        return false;
    }
    return true;
}

std::wstring Database::backup_file_path(int from_version) const {
    if (options_.backup_dir.empty()) return std::wstring();
    const std::wstring name = L"scheduler-" + db_utc_timestamp() + L"-v" + std::to_wstring(from_version) + L".db";
    return join_path(options_.backup_dir, name);
}

bool Database::migrate() {
    const int version = migrations::read_user_version(db_);
    if (version > DB_SCHEMA_VERSION) {
        last_error_ = L"Версия схемы БД (" + std::to_wstring(version) + L") новее поддерживаемой";
        return false;
    }

    // Базы, созданные до появления user_version, имеют версию 0 и таблицу tasks.
    const bool legacy = version == 0 && migrations::table_exists(db_, "tasks");
    if (version == 0 && !legacy) {
        migrations::create_schema_v2(db_);
        migrations::set_user_version(db_, DB_SCHEMA_VERSION);
        return true;
    }
    if (version == DB_SCHEMA_VERSION) {
        // Идемпотентность: досоздаём недостающие таблицы и индексы.
        migrations::create_schema_v2(db_);
        migrations::set_user_version(db_, DB_SCHEMA_VERSION);
        return true;
    }

    // Миграция v1 → v2: сначала резервная копия вне транзакции.
    const int source_version = legacy ? 1 : version;
    const std::wstring backup_path = backup_file_path(source_version);
    if (!backup_path.empty()) {
        std::wstring error;
        if (!db_ensure_directory(options_.backup_dir, error) || !backup_to(backup_path)) {
            last_error_ = L"Не удалось создать резервную копию перед миграцией: " + error + last_error_;
            return false;
        }
    }

    Transaction transaction(*this);
    migrations::migrate_v1_to_v2(db_);
    migrations::set_user_version(db_, DB_SCHEMA_VERSION);
    transaction.commit();

    // Подготовленные выражения могли устареть после ALTER TABLE.
    pool_->clear();

    AuditEntry entry;
    entry.actor_sid = u2ws(DB_LEGACY_OWNER_SID);
    entry.action = L"db.migrate";
    entry.object_type = L"database";
    entry.result = L"ok";
    entry.details = L"schema v" + std::to_wstring(source_version) + L" -> v" + std::to_wstring(DB_SCHEMA_VERSION);
    append_audit(entry);
    return true;
}

// ---- Tasks ----

std::vector<TaskDefinition> Database::list_tasks() {
    if (!tasks_) throw DbError("База данных не открыта");
    return tasks_->list();
}

std::vector<TaskDefinition> Database::list_tasks_by_owner(const std::wstring& owner_sid) {
    if (!tasks_) throw DbError("База данных не открыта");
    return tasks_->list(owner_sid);
}

std::optional<TaskDefinition> Database::get_task(const std::wstring& id) {
    if (!tasks_) throw DbError("База данных не открыта");
    return tasks_->get(id);
}

void Database::insert_task(const TaskDefinition& task) {
    if (!tasks_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    tasks_->insert(task);
}

void Database::update_task(const TaskDefinition& task) {
    if (!tasks_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    tasks_->update(task);
}

void Database::delete_task(const std::wstring& id) {
    if (!tasks_) throw DbError("База данных не открыта");
    Transaction transaction(*this);
    trigger_state_->remove_for_task(id);
    tasks_->remove(id);
    transaction.commit();
}

void Database::set_task_enabled(const std::wstring& id, bool enabled) {
    if (!tasks_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    tasks_->set_enabled(id, enabled);
}

int Database::task_count() {
    if (!tasks_) throw DbError("База данных не открыта");
    return tasks_->count();
}

// ---- TimeWindows ----

std::vector<TimeWindow> Database::get_time_windows(const std::wstring& task_id) {
    if (!windows_) throw DbError("База данных не открыта");
    return windows_->list(task_id);
}

void Database::delete_time_windows(const std::wstring& task_id) {
    if (!windows_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    windows_->remove_for_task(task_id);
}

void Database::insert_time_window(const TimeWindow& tw) {
    if (!windows_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    windows_->insert(tw);
}

void Database::replace_time_windows(const std::wstring& task_id, const std::vector<TimeWindow>& windows) {
    if (!windows_) throw DbError("База данных не открыта");
    Transaction transaction(*this);
    windows_->remove_for_task(task_id);
    trigger_state_->remove_for_task(task_id);
    for (const auto& window : windows) {
        TimeWindow copy = window;
        copy.task_id = task_id;
        windows_->insert(copy);
    }
    transaction.commit();
}

// ---- RunHistory ----

void Database::insert_history(const RunHistory& h, const std::wstring& window_id) {
    if (!history_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    history_->insert(h, window_id);
}

void Database::update_history(const RunHistory& h) {
    if (!history_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    history_->update(h);
}

std::vector<RunHistory> Database::get_history(const std::wstring& task_id, int limit) {
    if (!history_) throw DbError("База данных не открыта");
    return history_->list(task_id, limit);
}

std::vector<HistoryRecord> Database::get_history_records(const std::wstring& task_id, int limit) {
    if (!history_) throw DbError("База данных не открыта");
    return history_->list_records(task_id, limit);
}

void Database::trim_history(const std::wstring& task_id, int max_records) {
    if (!history_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    history_->trim(task_id, max_records);
}

// ---- Settings ----

std::optional<std::wstring> Database::get_setting(const std::wstring& key) {
    if (!settings_) throw DbError("База данных не открыта");
    return settings_->get(key);
}

int Database::get_setting_int(const std::wstring& key, int fallback) {
    if (!settings_) throw DbError("База данных не открыта");
    return settings_->get_int(key, fallback);
}

void Database::set_setting(const std::wstring& key, const std::wstring& value) {
    if (!settings_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    settings_->set(key, value);
}

std::vector<std::pair<std::wstring, std::wstring>> Database::list_settings() {
    if (!settings_) throw DbError("База данных не открыта");
    return settings_->list();
}

int Database::effective_setting_int(const std::wstring& key, int fallback) {
    try {
        return get_setting_int(key, fallback);
    } catch (...) {
        return fallback;
    }
}

// ---- Audit ----

void Database::append_audit(AuditEntry& entry) {
    if (!audit_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    audit_->append(entry);
}

std::vector<AuditEntry> Database::list_audit(const std::wstring& from, const std::wstring& to, int limit) {
    if (!audit_) throw DbError("База данных не открыта");
    return audit_->list(from, to, limit);
}

// ---- TriggerState ----

bool Database::mark_trigger_fired(const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at) {
    if (!trigger_state_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    return trigger_state_->mark_fired(task_id, window_id, slot, fired_at);
}

std::optional<std::wstring> Database::get_trigger_fired_at(
    const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot) {
    if (!trigger_state_) throw DbError("База данных не открыта");
    return trigger_state_->get_fired_at(task_id, window_id, slot);
}

std::vector<TriggerStateEntry> Database::list_trigger_state(const std::wstring& task_id) {
    if (!trigger_state_) throw DbError("База данных не открыта");
    return trigger_state_->list(task_id);
}

int Database::prune_trigger_state(const std::wstring& task_id, const std::wstring& before_slot) {
    if (!trigger_state_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    return trigger_state_->prune_before(task_id, before_slot);
}

int Database::clear_trigger_state(const std::wstring& task_id) {
    if (!trigger_state_) throw DbError("База данных не открыта");
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    return trigger_state_->remove_for_task(task_id);
}

// ---- Retention ----

int Database::purge_expired_outputs() {
    if (!db_ || options_.output_dir.empty()) return 0;
    const int days = effective_setting_int(L"retention.output_days", options_.output_retention_days);
    if (days <= 0) return 0;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(options_.output_dir);
    if (!fs::is_directory(root, ec)) return 0;

    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * days);
    int removed = 0;
    for (const auto& task_entry : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!task_entry.is_directory(ec)) continue;
        // Удаляем только каталоги задач с GUID-именем: путь строится из серверного GUID.
        if (!db_is_guid(task_entry.path().filename().wstring())) continue;

        for (const auto& file_entry : fs::directory_iterator(task_entry.path(), ec)) {
            if (ec) break;
            if (!file_entry.is_regular_file(ec)) continue;
            const fs::path file = file_entry.path();
            if (file.extension() != L".log") continue;
            if (!db_is_guid(file.stem().wstring())) continue;
            const auto modified = fs::last_write_time(file, ec);
            if (ec) continue;
            if (modified >= cutoff) continue;
            if (fs::remove(file, ec)) ++removed;
        }
    }
    return removed;
}

int Database::purge_expired_audit() {
    if (!audit_) return 0;
    const int days = effective_setting_int(L"retention.audit_days", options_.audit_retention_days);
    if (days <= 0) return 0;
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    return audit_->purge_before(db_utc_days_ago(days));
}

// ---- Backup / export ----

bool Database::backup_to(const std::wstring& path) {
    last_error_.clear();
    if (!db_) {
        last_error_ = L"База данных не открыта";
        return false;
    }
    if (transaction_depth_ > 0) {
        last_error_ = L"Резервная копия недоступна внутри транзакции";
        return false;
    }
    std::wstring error;
    if (!db_path_is_valid(path, error)) {
        last_error_ = error;
        return false;
    }
    if (!db_ensure_directory(parent_directory(path), error)) {
        last_error_ = error;
        return false;
    }

    sqlite3* destination = nullptr;
    const std::string path_utf8 = ws2u(path);
    if (sqlite3_open_v2(path_utf8.c_str(), &destination,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        last_error_ = L"Не удалось создать файл резервной копии";
        if (destination) sqlite3_close_v2(destination);
        return false;
    }

    bool ok = false;
    sqlite3_backup* backup = sqlite3_backup_init(destination, "main", db_, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        ok = sqlite3_backup_finish(backup) == SQLITE_OK;
    }
    if (!ok) last_error_ = L"Не удалось скопировать базу данных: " + u2ws(sqlite3_errmsg(destination));
    sqlite3_close_v2(destination);
    return ok;
}

bool Database::export_history_csv(const std::wstring& task_id, const std::wstring& path) {
    last_error_.clear();
    std::wstring error;
    if (!db_path_is_valid(path, error)) {
        last_error_ = error;
        return false;
    }
    if (!db_ensure_directory(parent_directory(path), error)) {
        last_error_ = error;
        return false;
    }

    std::vector<RunHistory> history;
    try {
        history = get_history(task_id, 10000);
    } catch (const std::exception& e) {
        last_error_ = u2ws(e.what());
        return false;
    }

    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        last_error_ = L"Не удалось открыть файл экспорта";
        return false;
    }

    // BOM: Excel корректно открывает UTF-8 с кириллицей.
    out << "\xEF\xBB\xBF";
    out << "id,task_id,start_time,end_time,exit_code,pid,status,error_message,output_path,triggered_by\n";
    for (const auto& item : history) {
        out << csv_field(item.id) << ','
            << csv_field(item.task_id) << ','
            << csv_field(item.start_time) << ','
            << csv_field(item.end_time.value_or(L"")) << ','
            << (item.exit_code.has_value() ? std::to_string(item.exit_code.value()) : std::string()) << ','
            << item.pid << ','
            << csv_field(ToWide(item.status)) << ','
            << csv_field(item.error_message) << ','
            << csv_field(item.output_path.value_or(L"")) << ','
            << csv_field(item.triggered_by == TriggeredBy::Manual ? L"Manual" : L"Schedule") << '\n';
    }
    out.flush();
    if (!out) {
        last_error_ = L"Не удалось записать файл экспорта";
        return false;
    }
    return true;
}
