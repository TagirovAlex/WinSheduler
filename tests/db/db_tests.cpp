// Тесты трека C: схема v2, миграции, репозитории, транзакции, retention,
// путь/ACL, резервное копирование, планы запросов и бенчмарк 1000 задач.
// Запуск: ./tests/db/run.ps1 (см. README.md в этом каталоге).

#include "database.h"
#include "db_path.h"
#include "db_stmt.h"
#include "db_util.h"
#include "migrations.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;
std::wstring g_log;

// Вывод одновременно в консоль и в отчёт ./temp/c/db-tests-report.txt.
void log(const std::wstring& text) {
    g_log += text;
    std::wcout << text;
}

void log_number(double value) { log(std::to_wstring(value)); }

// Открытие БД с фиксацией сообщения об ошибке: порядок вычисления аргументов
// в CHECK_MSG не гарантирован, поэтому last_error() читается после open().
bool open_ok(Database& db, const std::wstring& path, const DatabaseOptions& options = DatabaseOptions()) {
    const bool ok = db.open(path, options);
    if (!ok) log(L"  открытие БД не удалось: " + db.last_error() + L"\n");
    return ok;
}

void check_impl(bool ok, const char* expression, int line, const std::wstring& detail = std::wstring()) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::wstring message = L"  ОШИБКА (строка " + std::to_wstring(line) + L"): " + u2ws(expression);
    if (!detail.empty()) message += L" — " + detail;
    log(message + L"\n");
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)
#define CHECK_MSG(condition, detail) check_impl((condition), #condition, __LINE__, (detail))

std::wstring work_root() { return L"temp/c/db-tests"; }

std::wstring fresh_dir(const std::wstring& name) {
    const fs::path dir = fs::path(work_root()) / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    // Абсолютный путь: Database::open отклоняет относительные пути.
    return fs::absolute(dir).wstring();
}

std::wstring db_file(const std::wstring& dir) { return (fs::path(dir) / L"scheduler.db").wstring(); }

std::wstring join(const std::wstring& dir, const std::wstring& name) {
    return (fs::path(dir) / name).wstring();
}

// 12-значный числовой суффикс GUID для бенчмарка.
std::wstring pad12(int value) {
    std::wstring digits = std::to_wstring(value);
    return std::wstring(12 - digits.size(), L'0') + digits;
}

bool table_exists(sqlite3* db, const std::string& name) {
    return migrations::table_exists(db, name.c_str());
}

bool index_exists(sqlite3* db, const std::string& name) {
    const std::string sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='" + name + "'";
    return query_int(db, sql) == 1;
}

bool has_column(sqlite3* db, const char* table, const char* column) {
    return migrations::column_exists(db, table, column);
}

// Сбор плана запроса (все строки колонки detail).
std::wstring query_plan(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, ("EXPLAIN QUERY PLAN " + sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return L"<ошибка подготовки>";
    }
    std::wstring plan;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(stmt, 3);
        if (text) {
            plan += u2ws(reinterpret_cast<const char*>(text));
            plan += L"\n";
        }
    }
    sqlite3_finalize(stmt);
    return plan;
}

bool plan_uses_index(const std::wstring& plan, const std::wstring& index_name) {
    std::wstring lower = plan;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    std::wstring needle = index_name;
    for (auto& c : needle) c = static_cast<wchar_t>(towlower(c));
    return lower.find(needle) != std::wstring::npos;
}

TaskDefinition make_task(const std::wstring& id, const std::wstring& name, const std::wstring& owner) {
    TaskDefinition task;
    task.id = id;
    task.owner_sid = owner;
    task.name = name;
    task.description = L"Описание задачи";
    task.enabled = true;
    task.program_path = L"c:/scripts/backup.cmd";
    task.arguments = L"--full";
    task.working_directory = L"c:/scripts";
    task.window_style = WindowStyle::Hidden;
    task.session_mode = SessionMode::Headless;
    task.on_error = OnErrorAction::Retry;
    task.retry_count = 2;
    task.on_overlap = OnOverlapAction::WaitNext;
    task.timeout_minutes = 60;
    task.kill_on_timeout = true;
    task.log_output = true;
    task.max_history_records = 1000;
    task.created_at = L"2026-11-01T09:00:00Z";
    task.modified_at = L"2026-11-01T09:00:00Z";
    return task;
}

TimeWindow make_window(const std::wstring& id, TriggerKind kind) {
    TimeWindow window;
    window.id = id;
    window.kind = kind;
    window.start_date = L"2026-01-01";
    window.end_date = L"2026-12-31";
    window.days_of_week = { 1, 3 };
    window.days_of_month = { 1, 15 };
    window.specific_dates = { L"2026-05-01" };
    window.exact_times = { L"02:00", L"03:30" };
    window.start_time = L"09:00";
    window.end_time = L"18:00";
    window.repeat_interval_minutes = 15;
    window.repeat_until = L"2026-12-31T18:00";
    window.day_interval = 2;
    window.week_interval = 3;
    window.month_interval = 4;
    return window;
}

RunHistory make_history(const std::wstring& id, const std::wstring& task_id, const std::wstring& start_time,
    RunStatus status = RunStatus::Completed) {
    RunHistory history;
    history.id = id;
    history.task_id = task_id;
    history.start_time = start_time;
    history.end_time = L"2026-11-01T10:05:00Z";
    history.exit_code = 0;
    history.pid = 4242;
    history.status = status;
    history.error_message = L"";
    history.output_path = L"logs/output/" + task_id + L"/" + id + L".log";
    history.triggered_by = TriggeredBy::Schedule;
    return history;
}

// ---- C-1: схема и прагмы ----

void test_schema_v2() {
    log(L"[C-1] схема v2 и прагмы\n");
    const std::wstring dir = fresh_dir(L"schema");
    const std::wstring path = db_file(dir);

    {
        Database db;
        std::wstring error;
        CHECK(open_ok(db, path));
        CHECK(db.schema_version() == 2);

        CHECK_MSG(query_text(db.get_db(), "PRAGMA journal_mode") == "wal", L"journal_mode");
        CHECK(query_int(db.get_db(), "PRAGMA synchronous") == 1); // NORMAL
        CHECK(query_int(db.get_db(), "PRAGMA busy_timeout") == 5000);
        CHECK(query_int(db.get_db(), "PRAGMA foreign_keys") == 1);
        CHECK(query_text(db.get_db(), "PRAGMA quick_check") == "ok");

        for (const char* table : { "tasks", "time_windows", "trigger_state", "run_history", "audit_log", "settings" }) {
            CHECK_MSG(table_exists(db.get_db(), table), u2ws(table));
        }

        for (const char* column : { "owner_sid", "session_mode", "max_history_records", "created_at" }) {
            CHECK_MSG(has_column(db.get_db(), "tasks", column), u2ws(column));
        }
        for (const char* column : { "kind", "day_interval", "week_interval", "month_interval" }) {
            CHECK_MSG(has_column(db.get_db(), "time_windows", column), u2ws(column));
        }
        for (const char* column : { "window_id", "triggered_by" }) {
            CHECK_MSG(has_column(db.get_db(), "run_history", column), u2ws(column));
        }
        CHECK(has_column(db.get_db(), "trigger_state", "last_fired_at"));
        CHECK(has_column(db.get_db(), "audit_log", "actor_sid"));
        CHECK(has_column(db.get_db(), "settings", "value"));

        for (const char* index : { "idx_tasks_owner", "idx_tasks_enabled", "idx_time_windows_task",
                 "idx_trigger_state_task", "idx_run_history_task_start", "idx_run_history_status", "idx_audit_ts" }) {
            CHECK_MSG(index_exists(db.get_db(), index), u2ws(index));
        }
    }

    // Повторное открытие идемпотентно.
    {
        Database db;
        CHECK(open_ok(db, path));
        CHECK(db.schema_version() == 2);
        CHECK(table_exists(db.get_db(), "tasks"));
    }
}

// ---- C-2: репозитории ----

void test_task_repository() {
    log(L"[C-2] репозиторий задач\n");
    const std::wstring dir = fresh_dir(L"tasks");
    Database db;
    CHECK(open_ok(db, db_file(dir)));

    const std::wstring owner_a = L"S-1-5-21-100-200-300-1001";
    const std::wstring owner_b = L"S-1-5-21-100-200-300-1002";

    TaskDefinition task = make_task(L"11111111-1111-1111-1111-111111111111", L"Ночной бэкап", owner_a);
    RunAsCredentials credentials;
    credentials.user_name = L"svc_backup";
    credentials.domain = L"CONTOSO";
    credentials.encrypted_password = L"base64blob";
    task.run_as_user = credentials;
    db.insert_task(task);

    auto loaded = db.get_task(task.id);
    CHECK(loaded.has_value());
    if (loaded.has_value()) {
        CHECK(loaded->name == task.name);
        CHECK(loaded->description == task.description);
        CHECK(loaded->owner_sid == owner_a);
        CHECK(loaded->session_mode == SessionMode::Headless);
        CHECK(loaded->window_style == WindowStyle::Hidden);
        CHECK(loaded->retry_count == 2);
        CHECK(loaded->on_error == OnErrorAction::Retry);
        CHECK(loaded->on_overlap == OnOverlapAction::WaitNext);
        CHECK(loaded->timeout_minutes.has_value() && loaded->timeout_minutes.value() == 60);
        CHECK(loaded->kill_on_timeout);
        CHECK(loaded->log_output);
        CHECK(loaded->run_as_user.has_value());
        if (loaded->run_as_user.has_value()) {
            CHECK(loaded->run_as_user->user_name == L"svc_backup");
            CHECK(loaded->run_as_user->domain == L"CONTOSO");
            CHECK(loaded->run_as_user->encrypted_password == L"base64blob");
        }
    }

    TaskDefinition second = make_task(L"22222222-2222-2222-2222-222222222222", L"Отчёт", owner_b);
    second.run_as_user.reset();
    db.insert_task(second);
    CHECK(db.task_count() == 2);
    CHECK(db.list_tasks().size() == 2);
    CHECK(db.list_tasks_by_owner(owner_a).size() == 1);
    CHECK(db.list_tasks_by_owner(owner_b).size() == 1);
    CHECK(db.list_tasks_by_owner(L"S-1-5-18").empty());

    // owner_sid при обновлении не меняется.
    TaskDefinition changed = loaded.value_or(task);
    changed.name = L"Ночной бэкап (изменён)";
    changed.enabled = false;
    changed.owner_sid = owner_b;
    db.update_task(changed);
    auto after_update = db.get_task(task.id);
    CHECK(after_update.has_value());
    if (after_update.has_value()) {
        CHECK(after_update->name == L"Ночной бэкап (изменён)");
        CHECK(!after_update->enabled);
        CHECK_MSG(after_update->owner_sid == owner_a, L"owner_sid не должен меняться");
    }

    db.set_task_enabled(task.id, true);
    auto after_enable = db.get_task(task.id);
    CHECK(after_enable.has_value() && after_enable->enabled);

    // Удаление задачи каскадно удаляет окна и историю.
    TimeWindow cascade_window = make_window(L"33333333-3333-3333-3333-333333333333", TriggerKind::Daily);
    cascade_window.task_id = task.id;
    db.insert_time_window(cascade_window);
    db.insert_history(make_history(L"44444444-4444-4444-4444-444444444444", task.id, L"2026-11-01T10:00:00Z"));
    CHECK(db.get_time_windows(task.id).size() == 1);
    CHECK(db.get_history(task.id).size() == 1);
    db.delete_task(task.id);
    CHECK(!db.get_task(task.id).has_value());
    CHECK(db.get_time_windows(task.id).empty());
    CHECK(db.get_history(task.id).empty());
    CHECK(db.task_count() == 1);
}

void test_time_windows() {
    log(L"[C-2] репозиторий окон и состояние триггеров\n");
    const std::wstring dir = fresh_dir(L"windows");
    Database db;
    CHECK(open_ok(db, db_file(dir)));

    const std::wstring task_id = L"55555555-5555-5555-5555-555555555555";
    db.insert_task(make_task(task_id, L"Окна", L"S-1-5-18"));

    const TriggerKind kinds[] = { TriggerKind::Once, TriggerKind::Daily, TriggerKind::Weekly,
        TriggerKind::Monthly, TriggerKind::Interval };
    int index = 0;
    for (TriggerKind kind : kinds) {
        TimeWindow window = make_window(L"6000000" + std::to_wstring(index) + L"-0000-0000-0000-000000000000", kind);
        window.task_id = task_id;
        db.insert_time_window(window);
        ++index;
    }

    auto windows = db.get_time_windows(task_id);
    CHECK(windows.size() == 5);
    CHECK(windows.size() == 5 && windows[0].kind == TriggerKind::Once);
    CHECK(windows.size() == 5 && windows[4].kind == TriggerKind::Interval);
    if (windows.size() == 5) {
        const auto& interval = windows[4];
        CHECK(interval.days_of_week == std::vector<int>({ 1, 3 }));
        CHECK(interval.days_of_month == std::vector<int>({ 1, 15 }));
        CHECK(interval.specific_dates == std::vector<std::wstring>({ L"2026-05-01" }));
        CHECK(interval.exact_times == std::vector<std::wstring>({ L"02:00", L"03:30" }));
        CHECK(interval.start_date.has_value() && interval.start_date.value() == L"2026-01-01");
        CHECK(interval.start_time.has_value() && interval.start_time.value() == L"09:00");
        CHECK(interval.repeat_interval_minutes.has_value() && interval.repeat_interval_minutes.value() == 15);
        CHECK(interval.repeat_until.has_value() && interval.repeat_until.value() == L"2026-12-31T18:00");
        CHECK(interval.day_interval == 2);
        CHECK(interval.week_interval == 3);
        CHECK(interval.month_interval == 4);
    }

    // Легаси-колонка type: 1 только для интервала.
    const std::string type_sql =
        "SELECT type FROM time_windows WHERE task_id='" + ws2u(task_id) + "' AND kind=4";
    CHECK(query_int(db.get_db(), type_sql) == 1);
    const std::string daily_type_sql =
        "SELECT type FROM time_windows WHERE task_id='" + ws2u(task_id) + "' AND kind=1";
    CHECK(query_int(db.get_db(), daily_type_sql) == 0);

    // Замена набора триггеров очищает trigger_state.
    CHECK(db.mark_trigger_fired(task_id, L"70000000-0000-0000-0000-000000000001", L"2026-11-01T10:00",
        L"2026-11-01T10:00:05Z"));
    CHECK(!db.list_trigger_state(task_id).empty());
    db.replace_time_windows(task_id, { make_window(L"70000000-0000-0000-0000-000000000002", TriggerKind::Daily) });
    CHECK(db.get_time_windows(task_id).size() == 1);
    CHECK(db.list_trigger_state(task_id).empty());
}

void test_history_settings_audit() {
    log(L"[C-2] история, настройки, аудит\n");
    const std::wstring dir = fresh_dir(L"history");
    Database db;
    CHECK(open_ok(db, db_file(dir)));

    const std::wstring task_id = L"88888888-8888-8888-8888-888888888888";
    TaskDefinition task = make_task(task_id, L"История", L"S-1-5-18");
    task.max_history_records = 3;
    db.insert_task(task);

    for (int i = 0; i < 5; ++i) {
        RunHistory history = make_history(L"90000000-0000-0000-0000-00000000000" + std::to_wstring(i), task_id,
            L"2026-11-0" + std::to_wstring(i + 1) + L"T10:00:00Z");
        db.insert_history(history, L"70000000-0000-0000-0000-000000000003");
    }

    // Retention по max_history_records = 3: остаются три самые свежие записи.
    auto history = db.get_history(task_id, 100);
    CHECK(history.size() == 3);
    if (history.size() == 3) {
        CHECK(history[0].start_time == L"2026-11-05T10:00:00Z");
        CHECK(history[2].start_time == L"2026-11-03T10:00:00Z");
    }

    // window_id доступен через расширенное чтение.
    auto records = db.get_history_records(task_id, 1);
    CHECK(records.size() == 1);
    if (!records.empty()) {
        CHECK(records[0].window_id.has_value() &&
            records[0].window_id.value() == L"70000000-0000-0000-0000-000000000003");
        CHECK(records[0].history.triggered_by == TriggeredBy::Schedule);
    }

    // Обновление запуска: статус, код выхода, конец.
    RunHistory running = make_history(L"99999999-9999-9999-9999-999999999999", task_id, L"2026-11-06T10:00:00Z",
        RunStatus::Running);
    running.end_time.reset();
    running.exit_code.reset();
    db.insert_history(running, std::wstring());
    running.status = RunStatus::Timeout;
    running.exit_code = 5;
    running.error_message = L"Превышен таймаут";
    db.update_history(running);

    auto updated = db.get_history_records(task_id, 1);
    CHECK(updated.size() == 1);
    if (!updated.empty()) {
        CHECK(updated[0].history.status == RunStatus::Timeout);
        CHECK(updated[0].history.exit_code.has_value() && updated[0].history.exit_code.value() == 5);
        CHECK(updated[0].history.error_message == L"Превышен таймаут");
        CHECK(!updated[0].window_id.has_value());
    }

    db.trim_history(task_id, 1);
    CHECK(db.get_history(task_id, 100).size() == 1);

    // Настройки: перезапись и чтение.
    CHECK(!db.get_setting(L"service.version").has_value());
    db.set_setting(L"service.version", L"1.2.0");
    db.set_setting(L"service.version", L"1.3.0");
    auto version = db.get_setting(L"service.version");
    CHECK(version.has_value() && version.value() == L"1.3.0");
    db.set_setting(L"retention.output_days", L"7");
    CHECK(db.get_setting_int(L"retention.output_days", 30) == 7);
    CHECK(db.get_setting_int(L"нет.такого.ключа", 42) == 42);
    CHECK(db.list_settings().size() == 2);

    // Аудит: добавление и фильтр по времени.
    AuditEntry entry;
    entry.actor_sid = L"S-1-5-21-100-200-300-1001";
    entry.action = L"task.create";
    entry.object_type = L"task";
    entry.object_id = task_id;
    entry.result = L"ok";
    entry.details = L"";
    db.append_audit(entry);
    CHECK(!entry.ts.empty());

    auto audit = db.list_audit(L"", L"", 100);
    CHECK(audit.size() == 1);
    if (!audit.empty()) {
        CHECK(audit[0].actor_sid == L"S-1-5-21-100-200-300-1001");
        CHECK(audit[0].object_id.has_value() && audit[0].object_id.value() == task_id);
        CHECK(audit[0].result == L"ok");
    }
    CHECK(db.list_audit(L"2000-01-01T00:00:00Z", L"2000-01-02T00:00:00Z", 100).empty());
}

// ---- C-3: транзакции и один writer ----

void test_transactions_and_writers() {
    log(L"[C-3] транзакции и параллельная запись\n");
    const std::wstring dir = fresh_dir(L"transactions");
    Database db;
    CHECK(open_ok(db, db_file(dir)));

    const std::wstring task_id = L"aaaaaaaa-0000-0000-0000-000000000001";

    // RAII-rollback: без commit изменения не сохраняются.
    {
        Database::Transaction transaction(db);
        db.insert_task(make_task(task_id, L"Откат", L"S-1-5-18"));
        CHECK(db.get_task(task_id).has_value()); // видно внутри транзакции
    }
    CHECK(!db.get_task(task_id).has_value());

    // Commit сохраняет изменения.
    {
        Database::Transaction transaction(db);
        db.insert_task(make_task(task_id, L"Фиксация", L"S-1-5-18"));
        transaction.commit();
    }
    CHECK(db.get_task(task_id).has_value());

    // Вложенная транзакция запрещена.
    {
        Database::Transaction outer(db);
        bool thrown = false;
        try {
            Database::Transaction inner(db);
        } catch (const DbError&) {
            thrown = true;
        }
        CHECK(thrown);
        outer.commit();
    }

    // Параллельные писатели из нескольких потоков: все записи доходят.
    const int thread_count = 4;
    const int per_thread = 25;
    std::vector<std::thread> writers;
    std::vector<int> errors(thread_count, 0);
    for (int t = 0; t < thread_count; ++t) {
        writers.emplace_back([&db, &errors, t]() {
            try {
                for (int i = 0; i < per_thread; ++i) {
                    const std::wstring id = L"bbbbbbbb-0000-0000-0000-" + pad12(t * 100 + i);
                    db.insert_task(make_task(id, L"Параллельная", L"S-1-5-18"));
                }
            } catch (...) {
                errors[t] = 1;
            }
        });
    }
    for (auto& writer : writers) writer.join();
    for (int t = 0; t < thread_count; ++t) CHECK_MSG(errors[t] == 0, L"поток " + std::to_wstring(t));
    CHECK(db.task_count() == 1 + thread_count * per_thread);

    // Один и тот же слот срабатывает ровно один раз.
    const std::wstring window_id = L"cccccccc-0000-0000-0000-000000000001";
    const std::wstring slot = L"2026-11-01T10:00";
    std::vector<int> marked(thread_count, 0);
    std::vector<std::thread> markers;
    for (int t = 0; t < thread_count; ++t) {
        markers.emplace_back([&db, &marked, t, &task_id, &window_id, &slot]() {
            try {
                if (db.mark_trigger_fired(task_id, window_id, slot, L"2026-11-01T10:00:05Z")) marked[t] = 1;
            } catch (...) {
                marked[t] = -1;
            }
        });
    }
    for (auto& marker : markers) marker.join();
    int total_marked = 0;
    for (int value : marked) {
        CHECK_MSG(value >= 0, L"ошибка отметки слота");
        if (value == 1) ++total_marked;
    }
    CHECK(total_marked == 1);
    CHECK(db.list_trigger_state(task_id).size() == 1);

    // Повторная отметка того же слота возвращает false.
    CHECK(!db.mark_trigger_fired(task_id, window_id, slot, L"2026-11-01T10:01:00Z"));
    CHECK(db.prune_trigger_state(task_id, L"2026-11-02T00:00") == 1);
    CHECK(db.list_trigger_state(task_id).empty());
}

// ---- C-5: retention ----

void test_retention() {
    log(L"[C-5] retention истории и файлов вывода\n");
    const std::wstring dir = fresh_dir(L"retention");
    const std::wstring output_dir = join(dir, L"output");
    const std::wstring task_id = L"dddddddd-0000-0000-0000-000000000001";

    fs::create_directories(fs::path(output_dir) / task_id);
    fs::create_directories(fs::path(output_dir) / L"не-guid");
    const std::wstring old_file =
        join(join(output_dir, task_id), L"dddddddd-0000-0000-0000-0000000000aa.log");
    const std::wstring fresh_file =
        join(join(output_dir, task_id), L"dddddddd-0000-0000-0000-0000000000bb.log");
    const std::wstring foreign_file = join(join(output_dir, task_id), L"readme.txt");
    const std::wstring non_guid_file = join(join(output_dir, L"не-guid"), L"dddddddd-0000-0000-0000-0000000000cc.log");
    for (const auto& file : { old_file, fresh_file, foreign_file, non_guid_file }) {
        std::ofstream(file, std::ios::binary) << "log";
    }
    std::error_code ec;
    const auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(24 * 40);
    fs::last_write_time(fs::path(old_file), old_time, ec);

    DatabaseOptions options;
    options.output_dir = output_dir;
    options.output_retention_days = 30;
    options.audit_retention_days = 180;

    Database db;
    CHECK(open_ok(db, db_file(dir), options));

    // Старый файл вывода удалён при старте, свежий и чужие файлы остались.
    CHECK(!fs::exists(fs::path(old_file)));
    CHECK(fs::exists(fs::path(fresh_file)));
    CHECK(fs::exists(fs::path(foreign_file)));
    CHECK(fs::exists(fs::path(non_guid_file)));

    // Retention аудита: запись старше 180 дней удаляется.
    AuditEntry old_entry;
    old_entry.ts = L"2020-01-01T00:00:00Z";
    old_entry.actor_sid = L"S-1-5-18";
    old_entry.action = L"test.old";
    old_entry.object_type = L"test";
    old_entry.result = L"ok";
    db.append_audit(old_entry);
    AuditEntry fresh_entry = old_entry;
    fresh_entry.ts = db_utc_now();
    fresh_entry.action = L"test.fresh";
    db.append_audit(fresh_entry);
    CHECK(db.list_audit(L"", L"", 100).size() == 2);
    CHECK(db.purge_expired_audit() == 1);
    auto remaining = db.list_audit(L"", L"", 100);
    CHECK(remaining.size() == 1);
    if (!remaining.empty()) CHECK(remaining[0].action == L"test.fresh");
}

// ---- C-6: путь, ACL, backup, экспорт ----

void test_path_and_backup() {
    log(L"[C-6] валидация пути, резервная копия, экспорт\n");
    std::wstring error;

    CHECK(db_path_is_valid(L"c:/data/scheduler.db", error));
    CHECK(db_path_is_valid(L"c:/data/sub/scheduler.db", error));
    CHECK(!db_path_is_valid(L"//server/share/scheduler.db", error));
    CHECK(!db_path_is_valid(L"//?/c:/data/scheduler.db", error));
    CHECK(!db_path_is_valid(L"data/scheduler.db", error));
    CHECK(!db_path_is_valid(L"c:/data/../scheduler.db", error));
    CHECK(!db_path_is_valid(L"c:/data//scheduler.db", error));
    CHECK(!db_path_is_valid(L"c:/data/scheduler.db:stream", error));
    CHECK(!db_path_is_valid(L"c:/data/scheduler.db/", error));
    CHECK(!db_path_is_valid(L"", error));

    const std::wstring dir = fresh_dir(L"backup");
    const std::wstring nested = join(dir, L"a/b/c");
    CHECK(db_ensure_directory(nested, error));
    CHECK(fs::is_directory(fs::path(nested)));

    // ACL-проверка не должна падать; результат зависит от окружения.
    std::wstring acl_error;
    const bool acl_restricted = db_dir_acl_is_restricted(dir, acl_error);
    log(std::wstring(L"  ACL каталога temp: ") + (acl_restricted ? L"ограничен" : L"не ограничен") +
        L" (" + acl_error + L")\n");

    Database db;
    CHECK(open_ok(db, db_file(dir)));
    db.insert_task(make_task(L"eeeeeeee-0000-0000-0000-000000000001", L"Копия 1", L"S-1-5-18"));
    db.insert_task(make_task(L"eeeeeeee-0000-0000-0000-000000000002", L"Копия 2", L"S-1-5-18"));
    db.insert_history(make_history(L"eeeeeeee-0000-0000-0000-000000000003",
        L"eeeeeeee-0000-0000-0000-000000000001", L"2026-11-01T10:00:00Z"));

    const std::wstring backup_path = join(dir, L"backup/scheduler-test.db");
    CHECK_MSG(db.backup_to(backup_path), db.last_error());
    CHECK(fs::exists(fs::path(backup_path)));

    {
        Database backup;
        CHECK(open_ok(backup, backup_path));
        CHECK(backup.task_count() == 2);
        CHECK(backup.schema_version() == 2);
    }

    const std::wstring csv_path = join(dir, L"export/history.csv");
    CHECK_MSG(db.export_history_csv(L"eeeeeeee-0000-0000-0000-000000000001", csv_path), db.last_error());
    CHECK(fs::exists(fs::path(csv_path)));
    {
        std::ifstream csv(fs::path(csv_path), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(csv)), std::istreambuf_iterator<char>());
        CHECK(content.find("triggered_by") != std::string::npos);
        CHECK(content.find("eeeeeeee-0000-0000-0000-000000000003") != std::string::npos);
    }

    // Невалидный путь экспорта отклоняется.
    CHECK(!db.export_history_csv(L"eeeeeeee-0000-0000-0000-000000000001", L"relative.csv"));
    CHECK(!db.backup_to(L"//server/share/backup.db"));

    db.close();
    db.close(); // повторное закрытие безопасно
    CHECK(!db.is_open());
}

// ---- C-7: миграция v1 ----

void create_v1_database(const std::wstring& path, int user_version, bool with_settings,
    const std::wstring& owner_sid) {
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(ws2u(path).c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        throw DbError("Не удалось создать тестовую БД v1");
    }
    exec_sql(raw, R"SQL(
CREATE TABLE tasks (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL DEFAULT '',
    enabled INTEGER NOT NULL DEFAULT 1,
    program_path TEXT NOT NULL DEFAULT '',
    arguments TEXT NOT NULL DEFAULT '',
    working_directory TEXT NOT NULL DEFAULT '',
    window_style INTEGER NOT NULL DEFAULT 0,
    run_as_user TEXT,
    run_as_domain TEXT,
    encrypted_password TEXT,
    on_error INTEGER NOT NULL DEFAULT 0,
    retry_count INTEGER NOT NULL DEFAULT 0,
    on_overlap INTEGER NOT NULL DEFAULT 0,
    timeout_minutes INTEGER,
    kill_on_timeout INTEGER NOT NULL DEFAULT 0,
    log_output INTEGER NOT NULL DEFAULT 0,
    max_history_records INTEGER NOT NULL DEFAULT 1000,
    created_at TEXT NOT NULL DEFAULT '',
    modified_at TEXT NOT NULL DEFAULT ''
);
CREATE TABLE time_windows (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    type INTEGER NOT NULL DEFAULT 0,
    start_date TEXT,
    end_date TEXT,
    days_of_week TEXT NOT NULL DEFAULT '[]',
    days_of_month TEXT NOT NULL DEFAULT '[]',
    specific_dates TEXT NOT NULL DEFAULT '[]',
    exact_times TEXT NOT NULL DEFAULT '[]',
    start_time TEXT,
    end_time TEXT,
    repeat_interval_minutes INTEGER,
    repeat_until TEXT,
    FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
);
CREATE TABLE run_history (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    start_time TEXT NOT NULL DEFAULT '',
    end_time TEXT,
    exit_code INTEGER,
    pid INTEGER NOT NULL DEFAULT 0,
    status INTEGER NOT NULL DEFAULT 0,
    error_message TEXT NOT NULL DEFAULT '',
    output_path TEXT,
    FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
);
CREATE INDEX idx_time_windows_task ON time_windows(task_id);
CREATE INDEX idx_run_history_task ON run_history(task_id);
CREATE INDEX idx_run_history_start ON run_history(start_time);

INSERT INTO tasks (id,name,program_path,created_at,modified_at) VALUES
    ('11111111-1111-1111-1111-111111111111','Старая задача 1','c:/scripts/one.cmd','2025-01-01T00:00:00Z','2025-01-01T00:00:00Z'),
    ('22222222-2222-2222-2222-222222222222','Старая задача 2','c:/scripts/two.cmd','2025-01-02T00:00:00Z','2025-01-02T00:00:00Z');

INSERT INTO time_windows (id,task_id,type,days_of_week,days_of_month,exact_times) VALUES
    ('33333333-3333-3333-3333-333333333331','11111111-1111-1111-1111-111111111111',1,'[]','[]','["02:00"]'),
    ('33333333-3333-3333-3333-333333333332','11111111-1111-1111-1111-111111111111',0,'[]','[1,15]','["03:00"]'),
    ('33333333-3333-3333-3333-333333333333','11111111-1111-1111-1111-111111111111',0,'[1,3]','[]','["04:00"]'),
    ('33333333-3333-3333-3333-333333333334','11111111-1111-1111-1111-111111111111',0,'[]','[]','["05:00"]');

INSERT INTO run_history (id,task_id,start_time,status) VALUES
    ('44444444-4444-4444-4444-444444444441','11111111-1111-1111-1111-111111111111','2025-01-01T02:00:00Z',1);
)SQL");

    if (with_settings) {
        exec_sql(raw, "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        exec_sql(raw, "INSERT INTO settings (key,value) VALUES ('install.owner_sid','" + ws2u(owner_sid) + "')");
    }
    exec_sql(raw, "PRAGMA user_version = " + std::to_string(user_version));
    sqlite3_close_v2(raw);
}

void test_migration_v1() {
    log(L"[C-7] миграция v1 -> v2\n");

    // 1. Legacy-база без user_version, с install.owner_sid.
    const std::wstring dir = fresh_dir(L"migration-legacy");
    const std::wstring path = db_file(dir);
    const std::wstring owner = L"S-1-5-21-100-200-300-1001";
    create_v1_database(path, 0, true, owner);

    DatabaseOptions options;
    options.backup_dir = join(dir, L"backup");

    {
        Database db;
        CHECK(open_ok(db, path, options));
        CHECK_MSG(db.schema_version() == 2, L"версия после миграции");

        auto tasks = db.list_tasks();
        CHECK(tasks.size() == 2);
        for (const auto& task : tasks) {
            CHECK_MSG(task.owner_sid == owner, L"owner_sid из install.owner_sid");
            CHECK(task.session_mode == SessionMode::Headless);
        }

        auto windows = db.get_time_windows(L"11111111-1111-1111-1111-111111111111");
        CHECK(windows.size() == 4);
        std::vector<TriggerKind> kinds;
        for (const auto& window : windows) kinds.push_back(window.kind);
        CHECK(std::find(kinds.begin(), kinds.end(), TriggerKind::Interval) != kinds.end());
        CHECK(std::find(kinds.begin(), kinds.end(), TriggerKind::Monthly) != kinds.end());
        CHECK(std::find(kinds.begin(), kinds.end(), TriggerKind::Weekly) != kinds.end());
        CHECK(std::find(kinds.begin(), kinds.end(), TriggerKind::Daily) != kinds.end());
        for (const auto& window : windows) {
            CHECK(window.day_interval == 1);
            CHECK(window.week_interval == 1);
            CHECK(window.month_interval == 1);
        }

        auto history = db.get_history_records(L"11111111-1111-1111-1111-111111111111", 10);
        CHECK(history.size() == 1);
        if (!history.empty()) {
            CHECK(history[0].history.status == RunStatus::Completed);
            CHECK(!history[0].window_id.has_value());
            CHECK(history[0].history.triggered_by == TriggeredBy::Schedule);
        }

        // Новые таблицы созданы, аудит миграции записан.
        CHECK(table_exists(db.get_db(), "trigger_state"));
        CHECK(table_exists(db.get_db(), "audit_log"));
        CHECK(table_exists(db.get_db(), "settings"));
        auto audit = db.list_audit(L"", L"", 100);
        CHECK(audit.size() == 1);
        if (!audit.empty()) CHECK(audit[0].action == L"db.migrate");

        // Резервная копия создана до миграции.
        int backups = 0;
        for (const auto& entry : fs::directory_iterator(fs::path(options.backup_dir))) {
            if (entry.path().extension() == L".db") ++backups;
        }
        CHECK(backups == 1);
    }

    // 2. Повторное открытие: миграция не повторяется.
    {
        Database db;
        CHECK(open_ok(db, path, options));
        CHECK(db.schema_version() == 2);
        CHECK(db.list_tasks().size() == 2);
        CHECK(db.list_audit(L"", L"", 100).size() == 1);
    }

    // 3. База с user_version=1 и без settings: owner_sid = S-1-5-18.
    const std::wstring dir_v1 = fresh_dir(L"migration-v1");
    const std::wstring path_v1 = db_file(dir_v1);
    create_v1_database(path_v1, 1, false, L"");
    {
        Database db;
        DatabaseOptions options_v1;
        options_v1.backup_dir = join(dir_v1, L"backup");
        CHECK(open_ok(db, path_v1, options_v1));
        CHECK(db.schema_version() == 2);
        auto tasks = db.list_tasks();
        CHECK(tasks.size() == 2);
        for (const auto& task : tasks) CHECK_MSG(task.owner_sid == L"S-1-5-18", L"legacy SID");
    }
}

// ---- C-8: индексы, планы, бенчмарк ----

void test_query_plans_and_benchmark() {
    log(L"[C-8] планы запросов и бенчмарк 1000 задач\n");
    const std::wstring dir = fresh_dir(L"bench");
    Database db;
    CHECK(open_ok(db, db_file(dir)));

    // Планы запросов должны использовать индексы контракта.
    const auto plan_owner = query_plan(db.get_db(),
        "SELECT id,owner_sid FROM tasks WHERE owner_sid='S-1-5-18' ORDER BY name");
    const auto plan_windows =
        query_plan(db.get_db(), "SELECT id FROM time_windows WHERE task_id='x'");
    const auto plan_history = query_plan(db.get_db(),
        "SELECT id FROM run_history WHERE task_id='x' ORDER BY start_time DESC, id DESC LIMIT 10");
    const auto plan_status = query_plan(db.get_db(), "SELECT id FROM run_history WHERE status=1");
    const auto plan_audit = query_plan(db.get_db(), "SELECT id FROM audit_log ORDER BY ts DESC LIMIT 10");
    const auto plan_enabled = query_plan(db.get_db(), "SELECT id FROM tasks WHERE enabled=1");

    log(L"  план owner_sid: " + plan_owner);
    log(L"  план history: " + plan_history);

    CHECK_MSG(plan_uses_index(plan_owner, L"idx_tasks_owner"), L"idx_tasks_owner");
    CHECK_MSG(plan_uses_index(plan_windows, L"idx_time_windows_task"), L"idx_time_windows_task");
    CHECK_MSG(plan_uses_index(plan_history, L"idx_run_history_task_start"), L"idx_run_history_task_start");
    CHECK_MSG(plan_uses_index(plan_status, L"idx_run_history_status"), L"idx_run_history_status");
    CHECK_MSG(plan_uses_index(plan_audit, L"idx_audit_ts"), L"idx_audit_ts");
    CHECK_MSG(plan_uses_index(plan_enabled, L"idx_tasks_enabled"), L"idx_tasks_enabled");

    constexpr int task_total = 1000;
    const std::wstring owner = L"S-1-5-21-100-200-300-1001";

    const auto insert_start = std::chrono::steady_clock::now();
    {
        Database::Transaction transaction(db);
        for (int i = 0; i < task_total; ++i) {
            const std::wstring suffix = pad12(100000 + i);
            const std::wstring id = L"ffffffff-0000-0000-0000-" + suffix;
            TaskDefinition task = make_task(id, L"Задача " + std::to_wstring(i), owner);
            task.max_history_records = 50;
            db.insert_task(task);

            for (int w = 0; w < 2; ++w) {
                TimeWindow window = make_window(L"ffffffff-0000-0000-0001-" + suffix, TriggerKind::Daily);
                window.task_id = id;
                window.id = L"ffffffff-0000-0000-000" + std::to_wstring(w + 1) + L"-" + suffix;
                db.insert_time_window(window);
            }
            RunHistory history = make_history(L"ffffffff-0000-0000-0002-" + suffix, id, L"2026-11-01T10:00:00Z");
            db.insert_history(history);
        }
        transaction.commit();
    }
    const auto insert_end = std::chrono::steady_clock::now();
    const double insert_ms =
        std::chrono::duration<double, std::milli>(insert_end - insert_start).count();
    CHECK(db.task_count() == task_total);

    const auto list_start = std::chrono::steady_clock::now();
    const auto tasks = db.list_tasks();
    const auto list_end = std::chrono::steady_clock::now();
    const double list_ms = std::chrono::duration<double, std::milli>(list_end - list_start).count();
    CHECK(tasks.size() == static_cast<size_t>(task_total));

    const auto owned_start = std::chrono::steady_clock::now();
    const auto owned = db.list_tasks_by_owner(owner);
    const auto owned_end = std::chrono::steady_clock::now();
    const double owned_ms = std::chrono::duration<double, std::milli>(owned_end - owned_start).count();
    CHECK(owned.size() == static_cast<size_t>(task_total));

    const auto windows_start = std::chrono::steady_clock::now();
    size_t window_total = 0;
    for (const auto& task : tasks) window_total += db.get_time_windows(task.id).size();
    const auto windows_end = std::chrono::steady_clock::now();
    const double windows_ms = std::chrono::duration<double, std::milli>(windows_end - windows_start).count();
    CHECK(window_total == static_cast<size_t>(task_total) * 2);

    // Дашборд: последняя запись истории для каждой задачи.
    const auto history_start = std::chrono::steady_clock::now();
    size_t history_total = 0;
    for (const auto& task : tasks) history_total += db.get_history(task.id, 1).size();
    const auto history_end = std::chrono::steady_clock::now();
    const double history_ms = std::chrono::duration<double, std::milli>(history_end - history_start).count();
    CHECK(history_total == static_cast<size_t>(task_total));

    log(L"  вставка 1000 задач (+2000 окон, +1000 истории): " + std::to_wstring(insert_ms) + L" мс\n");
    log(L"  list_tasks (1000): " + std::to_wstring(list_ms) + L" мс\n");
    log(L"  list_tasks_by_owner (1000): " + std::to_wstring(owned_ms) + L" мс\n");
    log(L"  get_time_windows x1000: " + std::to_wstring(windows_ms) + L" мс\n");
    log(L"  get_history(1) x1000: " + std::to_wstring(history_ms) + L" мс\n");

    // Бюджет тика планировщика — вне зоны C; здесь фиксируем отсутствие деградации.
    CHECK_MSG(list_ms < 500.0, L"list_tasks медленнее 500 мс");
    CHECK_MSG(owned_ms < 500.0, L"list_tasks_by_owner медленнее 500 мс");
    CHECK_MSG(windows_ms < 2000.0, L"чтение окон медленнее 2000 мс");
    CHECK_MSG(history_ms < 2000.0, L"чтение истории медленнее 2000 мс");
}

} // namespace

int wmain() {
    log(L"Тесты трека C (хранение)\n\n");

    try {
        test_schema_v2();
        test_task_repository();
        test_time_windows();
        test_history_settings_audit();
        test_transactions_and_writers();
        test_retention();
        test_path_and_backup();
        test_migration_v1();
        test_query_plans_and_benchmark();
    } catch (const std::exception& e) {
        ++g_failures;
        log(L"НЕОБРАБОТАННОЕ ИСКЛЮЧЕНИЕ: " + u2ws(e.what()) + L"\n");
    } catch (...) {
        ++g_failures;
        log(L"НЕОБРАБОТАННОЕ ИСКЛЮЧЕНИЕ неизвестного типа\n");
    }

    log(L"\nПроверок: " + std::to_wstring(g_checks) + L", ошибок: " + std::to_wstring(g_failures) + L"\n");
    if (g_failures == 0) {
        log(L"Все тесты пройдены.\n");
    } else {
        log(L"Тесты НЕ пройдены.\n");
    }

    // Отчёт в UTF-8: консоль Windows может не отображать кириллицу.
    std::error_code ec;
    fs::create_directories(fs::path(work_root()), ec);
    std::ofstream report(fs::path(join(work_root(), L"db-tests-report.txt")), std::ios::binary | std::ios::trunc);
    if (report) report << ws2u(g_log);

    return g_failures == 0 ? 0 : 1;
}
