#include "db_repository.h"
#include "db_util.h"
#include "migrations.h"

#include <algorithm>

namespace {

// Явные списки колонок: SELECT * в новом коде запрещён (DB.md, раздел 5).
const char* kTaskColumns =
    "id,owner_sid,name,description,enabled,program_path,arguments,working_directory,"
    "window_style,session_mode,run_as_user,run_as_domain,encrypted_password,"
    "on_error,retry_count,on_overlap,timeout_minutes,kill_on_timeout,log_output,"
    "max_history_records,created_at,modified_at";

const char* kWindowColumns =
    "id,task_id,type,kind,start_date,end_date,days_of_week,days_of_month,"
    "specific_dates,exact_times,start_time,end_time,repeat_interval_minutes,repeat_until,"
    "day_interval,week_interval,month_interval";

const char* kHistoryColumns =
    "id,task_id,window_id,start_time,end_time,exit_code,pid,status,error_message,output_path,triggered_by";

const char* kAuditColumns = "id,ts,actor_sid,action,object_type,object_id,result,details";

std::string text_col(sqlite3_stmt* stmt, int index) {
    const auto* text = sqlite3_column_text(stmt, index);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

std::wstring wide_col(sqlite3_stmt* stmt, int index) { return u2ws(text_col(stmt, index)); }

// Старые сборки UI сохраняли пути с прямыми слэшами. Приводим их к
// каноническому Windows-виду при чтении, чтобы уже созданные задачи
// работали без повторного редактирования.
std::wstring native_path(const std::wstring& value) {
    std::wstring result = value;
    for (wchar_t& c : result) {
        if (c == L'/') c = L'\\';
    }
    return result;
}

std::optional<std::wstring> optional_wide_col(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) return std::nullopt;
    return wide_col(stmt, index);
}

std::optional<int> optional_int_col(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(stmt, index);
}

TaskDefinition read_task(sqlite3_stmt* stmt) {
    TaskDefinition task;
    task.id = wide_col(stmt, 0);
    task.owner_sid = wide_col(stmt, 1);
    task.name = wide_col(stmt, 2);
    task.description = wide_col(stmt, 3);
    task.enabled = sqlite3_column_int(stmt, 4) != 0;
    task.program_path = native_path(wide_col(stmt, 5));
    task.arguments = wide_col(stmt, 6);
    task.working_directory = native_path(wide_col(stmt, 7));
    task.window_style = static_cast<WindowStyle>(sqlite3_column_int(stmt, 8));
    task.session_mode = static_cast<SessionMode>(sqlite3_column_int(stmt, 9));
    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
        RunAsCredentials credentials;
        credentials.user_name = wide_col(stmt, 10);
        credentials.domain = wide_col(stmt, 11);
        credentials.encrypted_password = wide_col(stmt, 12);
        task.run_as_user = credentials;
    }
    task.on_error = static_cast<OnErrorAction>(sqlite3_column_int(stmt, 13));
    task.retry_count = sqlite3_column_int(stmt, 14);
    task.on_overlap = static_cast<OnOverlapAction>(sqlite3_column_int(stmt, 15));
    task.timeout_minutes = optional_int_col(stmt, 16);
    task.kill_on_timeout = sqlite3_column_int(stmt, 17) != 0;
    task.log_output = sqlite3_column_int(stmt, 18) != 0;
    task.max_history_records = sqlite3_column_int(stmt, 19);
    task.created_at = wide_col(stmt, 20);
    task.modified_at = wide_col(stmt, 21);
    return task;
}

void bind_optional_text(Statement& stmt, int index, const std::optional<std::wstring>& value) {
    if (value.has_value()) {
        stmt.bind_text(index, ws2u(value.value()));
    } else {
        stmt.bind_null(index);
    }
}

// Привязка полей задачи для INSERT (с owner_sid) и UPDATE (без owner_sid).
void bind_task_fields(Statement& stmt, const TaskDefinition& task, bool include_owner) {
    int index = 1;
    stmt.bind_text(index++, ws2u(task.id));
    if (include_owner) stmt.bind_text(index++, ws2u(task.owner_sid));
    stmt.bind_text(index++, ws2u(task.name));
    stmt.bind_text(index++, ws2u(task.description));
    stmt.bind_int(index++, task.enabled ? 1 : 0);
    stmt.bind_text(index++, ws2u(native_path(task.program_path)));
    stmt.bind_text(index++, ws2u(task.arguments));
    stmt.bind_text(index++, ws2u(native_path(task.working_directory)));
    stmt.bind_int(index++, static_cast<int>(task.window_style));
    stmt.bind_int(index++, static_cast<int>(task.session_mode));
    if (task.run_as_user.has_value()) {
        stmt.bind_text(index++, ws2u(task.run_as_user->user_name));
        stmt.bind_text(index++, ws2u(task.run_as_user->domain));
        stmt.bind_text(index++, ws2u(task.run_as_user->encrypted_password));
    } else {
        stmt.bind_null(index++);
        stmt.bind_null(index++);
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, static_cast<int>(task.on_error));
    stmt.bind_int(index++, task.retry_count);
    stmt.bind_int(index++, static_cast<int>(task.on_overlap));
    if (task.timeout_minutes.has_value()) {
        stmt.bind_int(index++, task.timeout_minutes.value());
    } else {
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, task.kill_on_timeout ? 1 : 0);
    stmt.bind_int(index++, task.log_output ? 1 : 0);
    stmt.bind_int(index++, task.max_history_records);
    stmt.bind_text(index++, ws2u(task.created_at));
    stmt.bind_text(index++, ws2u(task.modified_at));
}

TimeWindow read_window(sqlite3_stmt* stmt) {
    TimeWindow window;
    window.id = wide_col(stmt, 0);
    window.task_id = wide_col(stmt, 1);
    window.type = static_cast<TimeWindowType>(sqlite3_column_int(stmt, 2));
    window.kind = static_cast<TriggerKind>(sqlite3_column_int(stmt, 3));
    window.start_date = optional_wide_col(stmt, 4);
    window.end_date = optional_wide_col(stmt, 5);
    window.days_of_week = db_parse_int_array(text_col(stmt, 6).c_str());
    window.days_of_month = db_parse_int_array(text_col(stmt, 7).c_str());
    window.specific_dates = db_parse_string_array(text_col(stmt, 8).c_str());
    window.exact_times = db_parse_string_array(text_col(stmt, 9).c_str());
    window.start_time = optional_wide_col(stmt, 10);
    window.end_time = optional_wide_col(stmt, 11);
    window.repeat_interval_minutes = optional_int_col(stmt, 12);
    window.repeat_until = optional_wide_col(stmt, 13);
    window.day_interval = sqlite3_column_int(stmt, 14);
    window.week_interval = sqlite3_column_int(stmt, 15);
    window.month_interval = sqlite3_column_int(stmt, 16);
    return window;
}

RunHistory read_history(sqlite3_stmt* stmt, std::optional<std::wstring>* window_id) {
    RunHistory history;
    history.id = wide_col(stmt, 0);
    history.task_id = wide_col(stmt, 1);
    if (window_id) *window_id = optional_wide_col(stmt, 2);
    history.start_time = wide_col(stmt, 3);
    history.end_time = optional_wide_col(stmt, 4);
    history.exit_code = optional_int_col(stmt, 5);
    history.pid = sqlite3_column_int(stmt, 6);
    history.status = static_cast<RunStatus>(sqlite3_column_int(stmt, 7));
    history.error_message = wide_col(stmt, 8);
    history.output_path = optional_wide_col(stmt, 9);
    history.triggered_by = static_cast<TriggeredBy>(sqlite3_column_int(stmt, 10));
    return history;
}

AuditEntry read_audit(sqlite3_stmt* stmt) {
    AuditEntry entry;
    entry.id = sqlite3_column_int64(stmt, 0);
    entry.ts = wide_col(stmt, 1);
    entry.actor_sid = wide_col(stmt, 2);
    entry.action = wide_col(stmt, 3);
    entry.object_type = wide_col(stmt, 4);
    entry.object_id = optional_wide_col(stmt, 5);
    entry.result = wide_col(stmt, 6);
    entry.details = wide_col(stmt, 7);
    return entry;
}

} // namespace

TaskRepository::TaskRepository(StatementPool& pool) : pool_(pool) {}

std::vector<TaskDefinition> TaskRepository::list(const std::wstring& owner_sid) {
    const std::string sql = owner_sid.empty()
        ? std::string("SELECT ") + kTaskColumns + " FROM tasks ORDER BY name"
        : std::string("SELECT ") + kTaskColumns + " FROM tasks WHERE owner_sid=?1 ORDER BY name";

    Statement stmt(pool_, sql);
    if (!owner_sid.empty()) stmt.bind_text(1, ws2u(owner_sid));

    std::vector<TaskDefinition> tasks;
    while (stmt.step_row()) tasks.push_back(read_task(stmt.get()));
    return tasks;
}

std::optional<TaskDefinition> TaskRepository::get(const std::wstring& id) {
    const std::string sql = std::string("SELECT ") + kTaskColumns + " FROM tasks WHERE id=?1 COLLATE NOCASE";
    Statement stmt(pool_, sql);
    stmt.bind_text(1, ws2u(id));
    if (!stmt.step_row()) return std::nullopt;
    return read_task(stmt.get());
}

void TaskRepository::insert(const TaskDefinition& task) {
    // Задача без владельца (legacy-путь) помечается SID SYSTEM.
    TaskDefinition normalized = task;
    if (normalized.owner_sid.empty()) normalized.owner_sid = u2ws(DB_LEGACY_OWNER_SID);

    const std::string sql = std::string("INSERT INTO tasks (") + kTaskColumns + ") VALUES "
        "(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21,?22)";
    Statement stmt(pool_, sql);
    bind_task_fields(stmt, normalized, true);
    stmt.step_done();
}

void TaskRepository::update(const TaskDefinition& task) {
    // owner_sid и created_at не меняются (IPC.md, раздел 4).
    const std::string sql = "UPDATE tasks SET name=?2,description=?3,enabled=?4,program_path=?5,"
        "arguments=?6,working_directory=?7,window_style=?8,session_mode=?9,run_as_user=?10,"
        "run_as_domain=?11,encrypted_password=?12,on_error=?13,retry_count=?14,on_overlap=?15,"
        "timeout_minutes=?16,kill_on_timeout=?17,log_output=?18,max_history_records=?19,"
        "modified_at=?20 WHERE id=?1 COLLATE NOCASE";
    Statement stmt(pool_, sql);
    // Порядок параметров bind_task_fields: id, [owner], name, ... — owner пропускаем.
    int index = 1;
    stmt.bind_text(index++, ws2u(task.id));
    stmt.bind_text(index++, ws2u(task.name));
    stmt.bind_text(index++, ws2u(task.description));
    stmt.bind_int(index++, task.enabled ? 1 : 0);
    stmt.bind_text(index++, ws2u(native_path(task.program_path)));
    stmt.bind_text(index++, ws2u(task.arguments));
    stmt.bind_text(index++, ws2u(native_path(task.working_directory)));
    stmt.bind_int(index++, static_cast<int>(task.window_style));
    stmt.bind_int(index++, static_cast<int>(task.session_mode));
    if (task.run_as_user.has_value()) {
        stmt.bind_text(index++, ws2u(task.run_as_user->user_name));
        stmt.bind_text(index++, ws2u(task.run_as_user->domain));
        stmt.bind_text(index++, ws2u(task.run_as_user->encrypted_password));
    } else {
        stmt.bind_null(index++);
        stmt.bind_null(index++);
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, static_cast<int>(task.on_error));
    stmt.bind_int(index++, task.retry_count);
    stmt.bind_int(index++, static_cast<int>(task.on_overlap));
    if (task.timeout_minutes.has_value()) {
        stmt.bind_int(index++, task.timeout_minutes.value());
    } else {
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, task.kill_on_timeout ? 1 : 0);
    stmt.bind_int(index++, task.log_output ? 1 : 0);
    stmt.bind_int(index++, task.max_history_records);
    stmt.bind_text(index++, ws2u(task.modified_at));
    stmt.step_done();
}

void TaskRepository::remove(const std::wstring& id) {
    Statement stmt(pool_, "DELETE FROM tasks WHERE id=?1 COLLATE NOCASE");
    stmt.bind_text(1, ws2u(id));
    stmt.step_done();
}

void TaskRepository::set_enabled(const std::wstring& id, bool enabled) {
    Statement stmt(pool_, "UPDATE tasks SET enabled=?2 WHERE id=?1 COLLATE NOCASE");
    stmt.bind_text(1, ws2u(id));
    stmt.bind_int(2, enabled ? 1 : 0);
    stmt.step_done();
}

int TaskRepository::count() {
    Statement stmt(pool_, "SELECT COUNT(*) FROM tasks");
    return stmt.step_row() ? sqlite3_column_int(stmt.get(), 0) : 0;
}

std::optional<int> TaskRepository::max_history_records(const std::wstring& task_id) {
    Statement stmt(pool_, "SELECT max_history_records FROM tasks WHERE id=?1 COLLATE NOCASE");
    stmt.bind_text(1, ws2u(task_id));
    if (!stmt.step_row()) return std::nullopt;
    return sqlite3_column_int(stmt.get(), 0);
}

TimeWindowRepository::TimeWindowRepository(StatementPool& pool) : pool_(pool) {}

std::vector<TimeWindow> TimeWindowRepository::list(const std::wstring& task_id) {
    const std::string sql =
        std::string("SELECT ") + kWindowColumns + " FROM time_windows WHERE task_id=?1 COLLATE NOCASE";
    Statement stmt(pool_, sql);
    stmt.bind_text(1, ws2u(task_id));

    std::vector<TimeWindow> windows;
    while (stmt.step_row()) windows.push_back(read_window(stmt.get()));
    return windows;
}

void TimeWindowRepository::insert(const TimeWindow& window) {
    const std::string sql = std::string("INSERT INTO time_windows (") + kWindowColumns + ") VALUES "
        "(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)";
    Statement stmt(pool_, sql);

    int index = 1;
    stmt.bind_text(index++, ws2u(window.id));
    stmt.bind_text(index++, ws2u(window.task_id));
    // Легаси-колонка type сохраняется: 0 — точные времена, 1 — интервал.
    stmt.bind_int(index++, window.kind == TriggerKind::Interval ? 1 : 0);
    stmt.bind_int(index++, static_cast<int>(window.kind));
    bind_optional_text(stmt, index++, window.start_date);
    bind_optional_text(stmt, index++, window.end_date);
    stmt.bind_text(index++, db_json_int_array(window.days_of_week));
    stmt.bind_text(index++, db_json_int_array(window.days_of_month));
    stmt.bind_text(index++, db_json_string_array(window.specific_dates));
    stmt.bind_text(index++, db_json_string_array(window.exact_times));
    bind_optional_text(stmt, index++, window.start_time);
    bind_optional_text(stmt, index++, window.end_time);
    if (window.repeat_interval_minutes.has_value()) {
        stmt.bind_int(index++, window.repeat_interval_minutes.value());
    } else {
        stmt.bind_null(index++);
    }
    bind_optional_text(stmt, index++, window.repeat_until);
    stmt.bind_int(index++, window.day_interval);
    stmt.bind_int(index++, window.week_interval);
    stmt.bind_int(index++, window.month_interval);
    stmt.step_done();
}

void TimeWindowRepository::remove_for_task(const std::wstring& task_id) {
    Statement stmt(pool_, "DELETE FROM time_windows WHERE task_id=?1 COLLATE NOCASE");
    stmt.bind_text(1, ws2u(task_id));
    stmt.step_done();
}

HistoryRepository::HistoryRepository(StatementPool& pool) : pool_(pool) {}

void HistoryRepository::insert(const RunHistory& history, const std::wstring& window_id) {
    const std::string sql = std::string("INSERT INTO run_history (") + kHistoryColumns + ") VALUES "
        "(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)";
    Statement stmt(pool_, sql);

    int index = 1;
    stmt.bind_text(index++, ws2u(history.id));
    stmt.bind_text(index++, ws2u(history.task_id));
    if (window_id.empty()) {
        stmt.bind_null(index++);
    } else {
        stmt.bind_text(index++, ws2u(window_id));
    }
    stmt.bind_text(index++, ws2u(history.start_time));
    bind_optional_text(stmt, index++, history.end_time);
    if (history.exit_code.has_value()) {
        stmt.bind_int(index++, history.exit_code.value());
    } else {
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, history.pid);
    stmt.bind_int(index++, static_cast<int>(history.status));
    stmt.bind_text(index++, ws2u(history.error_message));
    bind_optional_text(stmt, index++, history.output_path);
    stmt.bind_int(index++, static_cast<int>(history.triggered_by));
    stmt.step_done();

    // Retention по задаче: лишние записи удаляются сразу после вставки (DB.md, раздел 6).
    Statement trim_stmt(pool_,
        "DELETE FROM run_history WHERE task_id=?1 AND id NOT IN ("
        "SELECT id FROM run_history WHERE task_id=?1 ORDER BY start_time DESC, id DESC "
        "LIMIT COALESCE((SELECT max_history_records FROM tasks WHERE id=?1), 1000))");
    trim_stmt.bind_text(1, ws2u(history.task_id));
    trim_stmt.step_done();
}

void HistoryRepository::update(const RunHistory& history) {
    Statement stmt(pool_,
        "UPDATE run_history SET end_time=?2,exit_code=?3,pid=?4,status=?5,error_message=?6,"
        "output_path=?7 WHERE id=?1");
    int index = 1;
    stmt.bind_text(index++, ws2u(history.id));
    bind_optional_text(stmt, index++, history.end_time);
    if (history.exit_code.has_value()) {
        stmt.bind_int(index++, history.exit_code.value());
    } else {
        stmt.bind_null(index++);
    }
    stmt.bind_int(index++, history.pid);
    stmt.bind_int(index++, static_cast<int>(history.status));
    stmt.bind_text(index++, ws2u(history.error_message));
    bind_optional_text(stmt, index++, history.output_path);
    stmt.step_done();
}

std::vector<HistoryRecord> HistoryRepository::list_records(const std::wstring& task_id, int limit) {
    const std::string sql = std::string("SELECT ") + kHistoryColumns +
        " FROM run_history WHERE task_id=?1 COLLATE NOCASE ORDER BY start_time DESC, id DESC LIMIT ?2";
    Statement stmt(pool_, sql);
    stmt.bind_text(1, ws2u(task_id));
    stmt.bind_int(2, (std::max)(1, limit));

    std::vector<HistoryRecord> records;
    while (stmt.step_row()) {
        HistoryRecord record;
        record.history = read_history(stmt.get(), &record.window_id);
        records.push_back(std::move(record));
    }
    return records;
}

std::vector<RunHistory> HistoryRepository::list(const std::wstring& task_id, int limit) {
    std::vector<HistoryRecord> records = list_records(task_id, limit);
    std::vector<RunHistory> history;
    history.reserve(records.size());
    for (auto& record : records) history.push_back(std::move(record.history));
    return history;
}

int HistoryRepository::trim(const std::wstring& task_id, int max_records) {
    const int keep = std::clamp(max_records, 1, 10000);
    Statement stmt(pool_,
        "DELETE FROM run_history WHERE task_id=?1 AND id NOT IN ("
        "SELECT id FROM run_history WHERE task_id=?1 ORDER BY start_time DESC, id DESC LIMIT ?2)");
    stmt.bind_text(1, ws2u(task_id));
    stmt.bind_int(2, keep);
    stmt.step_done();
    return stmt.changes();
}

SettingsRepository::SettingsRepository(StatementPool& pool) : pool_(pool) {}

std::optional<std::wstring> SettingsRepository::get(const std::wstring& key) {
    Statement stmt(pool_, "SELECT value FROM settings WHERE key=?1");
    stmt.bind_text(1, ws2u(key));
    if (!stmt.step_row()) return std::nullopt;
    return wide_col(stmt.get(), 0);
}

int SettingsRepository::get_int(const std::wstring& key, int fallback) {
    const auto value = get(key);
    if (!value.has_value()) return fallback;
    try {
        return std::stoi(value.value());
    } catch (...) {
        return fallback;
    }
}

void SettingsRepository::set(const std::wstring& key, const std::wstring& value) {
    Statement stmt(pool_,
        "INSERT INTO settings (key,value) VALUES (?1,?2) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    stmt.bind_text(1, ws2u(key));
    stmt.bind_text(2, ws2u(value));
    stmt.step_done();
}

std::vector<std::pair<std::wstring, std::wstring>> SettingsRepository::list() {
    Statement stmt(pool_, "SELECT key,value FROM settings ORDER BY key");
    std::vector<std::pair<std::wstring, std::wstring>> result;
    while (stmt.step_row()) result.emplace_back(wide_col(stmt.get(), 0), wide_col(stmt.get(), 1));
    return result;
}

AuditRepository::AuditRepository(StatementPool& pool) : pool_(pool) {}

void AuditRepository::append(AuditEntry& entry) {
    if (entry.ts.empty()) entry.ts = db_utc_now();
    Statement stmt(pool_,
        "INSERT INTO audit_log (ts,actor_sid,action,object_type,object_id,result,details) "
        "VALUES (?1,?2,?3,?4,?5,?6,?7)");
    stmt.bind_text(1, ws2u(entry.ts));
    stmt.bind_text(2, ws2u(entry.actor_sid));
    stmt.bind_text(3, ws2u(entry.action));
    stmt.bind_text(4, ws2u(entry.object_type));
    bind_optional_text(stmt, 5, entry.object_id);
    stmt.bind_text(6, ws2u(entry.result));
    stmt.bind_text(7, ws2u(entry.details));
    stmt.step_done();
}

std::vector<AuditEntry> AuditRepository::list(const std::wstring& from, const std::wstring& to, int limit) {
    const std::string sql = std::string("SELECT ") + kAuditColumns +
        " FROM audit_log WHERE (?1='' OR ts>=?1) AND (?2='' OR ts<=?2) ORDER BY ts DESC LIMIT ?3";
    Statement stmt(pool_, sql);
    stmt.bind_text(1, ws2u(from));
    stmt.bind_text(2, ws2u(to));
    stmt.bind_int(3, (std::max)(1, limit));

    std::vector<AuditEntry> entries;
    while (stmt.step_row()) entries.push_back(read_audit(stmt.get()));
    return entries;
}

int AuditRepository::purge_before(const std::wstring& ts) {
    Statement stmt(pool_, "DELETE FROM audit_log WHERE ts<?1");
    stmt.bind_text(1, ws2u(ts));
    stmt.step_done();
    return stmt.changes();
}

long long AuditRepository::count() {
    Statement stmt(pool_, "SELECT COUNT(*) FROM audit_log");
    return stmt.step_row() ? sqlite3_column_int64(stmt.get(), 0) : 0;
}

TriggerStateRepository::TriggerStateRepository(StatementPool& pool) : pool_(pool) {}

bool TriggerStateRepository::mark_fired(const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at) {
    Statement stmt(pool_,
        "INSERT OR IGNORE INTO trigger_state (task_id,window_id,slot,last_fired_at) VALUES (?1,?2,?3,?4)");
    stmt.bind_text(1, ws2u(task_id));
    stmt.bind_text(2, ws2u(window_id));
    stmt.bind_text(3, ws2u(slot));
    stmt.bind_text(4, ws2u(fired_at));
    stmt.step_done();
    return stmt.changes() > 0;
}

std::optional<std::wstring> TriggerStateRepository::get_fired_at(
    const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot) {
    Statement stmt(pool_,
        "SELECT last_fired_at FROM trigger_state WHERE task_id=?1 AND window_id=?2 AND slot=?3");
    stmt.bind_text(1, ws2u(task_id));
    stmt.bind_text(2, ws2u(window_id));
    stmt.bind_text(3, ws2u(slot));
    if (!stmt.step_row()) return std::nullopt;
    return wide_col(stmt.get(), 0);
}

std::vector<TriggerStateEntry> TriggerStateRepository::list(const std::wstring& task_id) {
    Statement stmt(pool_,
        "SELECT task_id,window_id,slot,last_fired_at FROM trigger_state WHERE task_id=?1 ORDER BY slot");
    stmt.bind_text(1, ws2u(task_id));

    std::vector<TriggerStateEntry> entries;
    while (stmt.step_row()) {
        TriggerStateEntry entry;
        entry.task_id = wide_col(stmt.get(), 0);
        entry.window_id = wide_col(stmt.get(), 1);
        entry.slot = wide_col(stmt.get(), 2);
        entry.last_fired_at = wide_col(stmt.get(), 3);
        entries.push_back(std::move(entry));
    }
    return entries;
}

int TriggerStateRepository::prune_before(const std::wstring& task_id, const std::wstring& slot) {
    Statement stmt(pool_, "DELETE FROM trigger_state WHERE task_id=?1 AND slot<?2");
    stmt.bind_text(1, ws2u(task_id));
    stmt.bind_text(2, ws2u(slot));
    stmt.step_done();
    return stmt.changes();
}

int TriggerStateRepository::remove_for_task(const std::wstring& task_id) {
    Statement stmt(pool_, "DELETE FROM trigger_state WHERE task_id=?1");
    stmt.bind_text(1, ws2u(task_id));
    stmt.step_done();
    return stmt.changes();
}
