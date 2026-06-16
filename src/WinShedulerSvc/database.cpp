#include "database.h"
#include "json.h"
#include "sqlite3.h"
#include <sstream>
#include <codecvt>
#include <locale>
#include <cwctype>
#include <rpc.h>
#include <rpcdce.h>

#pragma comment(lib, "rpcrt4.lib")

static std::wstring guid_to_wstring(GUID& guid) {
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    std::wstring s(buf);
    // Strip { and }
    if (s.front() == L'{') s = s.substr(1);
    if (s.back() == L'}') s.pop_back();
    // Lowercase to match C# Guid.ToString()
    for (auto& c : s) c = towlower(c);
    return s;
}

Database::Database() = default;
Database::~Database() { close(); }

static std::string ws2s_impl(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring s2ws_impl(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

std::string Database::ws2s(const std::wstring& ws) { return ws2s_impl(ws); }
std::wstring Database::s2ws(const std::string& s) { return s2ws_impl(s); }

static const char* safe_col_text(sqlite3_stmt* stmt, int col) {
    auto* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

static void check_prepare(sqlite3* db, int rc, sqlite3_stmt* stmt) {
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        if (stmt) sqlite3_finalize(stmt);
        throw std::runtime_error("SQLite prepare failed: " + msg);
    }
}

static void check_step(sqlite3* db, int rc, sqlite3_stmt* stmt) {
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error("SQLite step failed: " + msg);
    }
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQLite error: " + msg);
    }
}

void Database::ensure_schema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS tasks (
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
        CREATE TABLE IF NOT EXISTS time_windows (
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
        CREATE TABLE IF NOT EXISTS run_history (
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
        CREATE INDEX IF NOT EXISTS idx_time_windows_task ON time_windows(task_id);
        CREATE INDEX IF NOT EXISTS idx_run_history_task ON run_history(task_id);
        CREATE INDEX IF NOT EXISTS idx_run_history_start ON run_history(start_time);
    )");
}

std::wstring Database::generate_guid() {
    GUID guid;
    UuidCreate(&guid);
    return guid_to_wstring(guid);
}

bool Database::open(const std::wstring& path) {
    auto path_utf8 = ws2s(path);
    if (sqlite3_open(path_utf8.c_str(), &db_) != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    ensure_schema();
    return true;
}

void Database::close() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

// ---- Tasks ----

std::vector<TaskDefinition> Database::list_tasks() {
    std::vector<TaskDefinition> tasks;
    auto sql = "SELECT * FROM tasks ORDER BY name";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        check_step(db_, rc, stmt);
        TaskDefinition t;
        t.id = s2ws(safe_col_text(stmt, 0));
        t.name = s2ws(safe_col_text(stmt, 1));
        t.description = s2ws(safe_col_text(stmt, 2));
        t.enabled = sqlite3_column_int(stmt, 3) != 0;
        t.program_path = s2ws(safe_col_text(stmt, 4));
        t.arguments = s2ws(safe_col_text(stmt, 5));
        t.working_directory = s2ws(safe_col_text(stmt, 6));
        t.window_style = static_cast<WindowStyle>(sqlite3_column_int(stmt, 7));
        auto* user = sqlite3_column_text(stmt, 8);
        if (user) {
            RunAsCredentials cred;
            cred.user_name = s2ws((const char*)user);
            cred.domain = s2ws(safe_col_text(stmt, 9));
            auto* pwd = sqlite3_column_text(stmt, 10);
            cred.encrypted_password = pwd ? s2ws((const char*)pwd) : L"";
            t.run_as_user = cred;
        }
        t.on_error = static_cast<OnErrorAction>(sqlite3_column_int(stmt, 11));
        t.retry_count = sqlite3_column_int(stmt, 12);
        t.on_overlap = static_cast<OnOverlapAction>(sqlite3_column_int(stmt, 13));
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
            t.timeout_minutes = sqlite3_column_int(stmt, 14);
        t.kill_on_timeout = sqlite3_column_int(stmt, 15) != 0;
        t.log_output = sqlite3_column_int(stmt, 16) != 0;
        t.max_history_records = sqlite3_column_int(stmt, 17);
        t.created_at = s2ws(safe_col_text(stmt, 18));
        t.modified_at = s2ws(safe_col_text(stmt, 19));
        tasks.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);
    return tasks;
}

std::optional<TaskDefinition> Database::get_task(const std::wstring& id) {
    auto sql = "SELECT * FROM tasks WHERE id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto id_u8 = ws2s(id);
    sqlite3_bind_text(stmt, 1, id_u8.c_str(), -1, SQLITE_TRANSIENT);
    TaskDefinition t;
    bool found = false;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        found = true;
        t.id = id;
        t.name = s2ws(safe_col_text(stmt, 1));
        t.description = s2ws(safe_col_text(stmt, 2));
        t.enabled = sqlite3_column_int(stmt, 3) != 0;
        t.program_path = s2ws(safe_col_text(stmt, 4));
        t.arguments = s2ws(safe_col_text(stmt, 5));
        t.working_directory = s2ws(safe_col_text(stmt, 6));
        t.window_style = static_cast<WindowStyle>(sqlite3_column_int(stmt, 7));
        auto* user = sqlite3_column_text(stmt, 8);
        if (user) {
            RunAsCredentials cred;
            cred.user_name = s2ws((const char*)user);
            auto* dom = sqlite3_column_text(stmt, 9);
            cred.domain = dom ? s2ws((const char*)dom) : L"";
            auto* pwd = sqlite3_column_text(stmt, 10);
            cred.encrypted_password = pwd ? s2ws((const char*)pwd) : L"";
            t.run_as_user = cred;
        }
        t.on_error = static_cast<OnErrorAction>(sqlite3_column_int(stmt, 11));
        t.retry_count = sqlite3_column_int(stmt, 12);
        t.on_overlap = static_cast<OnOverlapAction>(sqlite3_column_int(stmt, 13));
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
            t.timeout_minutes = sqlite3_column_int(stmt, 14);
        t.kill_on_timeout = sqlite3_column_int(stmt, 15) != 0;
        t.log_output = sqlite3_column_int(stmt, 16) != 0;
        t.max_history_records = sqlite3_column_int(stmt, 17);
        t.created_at = s2ws(safe_col_text(stmt, 18));
        t.modified_at = s2ws(safe_col_text(stmt, 19));
    }
    sqlite3_finalize(stmt);
    if (found) return t;
    return std::nullopt;
}

void Database::insert_task(const TaskDefinition& task) {
    auto sql = "INSERT INTO tasks (id,name,description,enabled,program_path,arguments,working_directory,"
        "window_style,run_as_user,run_as_domain,encrypted_password,on_error,retry_count,"
        "on_overlap,timeout_minutes,kill_on_timeout,log_output,max_history_records,created_at,modified_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto id_u8 = ws2s(task.id);
    sqlite3_bind_text(stmt, 1, id_u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws2s(task.name).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ws2s(task.description).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, task.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 5, ws2s(task.program_path).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, ws2s(task.arguments).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, ws2s(task.working_directory).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, static_cast<int>(task.window_style));
    if (task.run_as_user.has_value()) {
        sqlite3_bind_text(stmt, 9, ws2s(task.run_as_user->user_name).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, ws2s(task.run_as_user->domain).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, ws2s(task.run_as_user->encrypted_password).c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, 12, static_cast<int>(task.on_error));
    sqlite3_bind_int(stmt, 13, task.retry_count);
    sqlite3_bind_int(stmt, 14, static_cast<int>(task.on_overlap));
    if (task.timeout_minutes.has_value()) sqlite3_bind_int(stmt, 15, task.timeout_minutes.value());
    else sqlite3_bind_null(stmt, 15);
    sqlite3_bind_int(stmt, 16, task.kill_on_timeout ? 1 : 0);
    sqlite3_bind_int(stmt, 17, task.log_output ? 1 : 0);
    sqlite3_bind_int(stmt, 18, task.max_history_records);
    sqlite3_bind_text(stmt, 19, ws2s(task.created_at).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, ws2s(task.modified_at).c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

void Database::update_task(const TaskDefinition& task) {
    auto sql = "UPDATE tasks SET name=?,description=?,enabled=?,program_path=?,arguments=?,"
        "working_directory=?,window_style=?,run_as_user=?,run_as_domain=?,encrypted_password=?,"
        "on_error=?,retry_count=?,on_overlap=?,timeout_minutes=?,kill_on_timeout=?,log_output=?,"
        "max_history_records=?,modified_at=? WHERE id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    sqlite3_bind_text(stmt, 1, ws2s(task.name).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws2s(task.description).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, task.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 4, ws2s(task.program_path).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, ws2s(task.arguments).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, ws2s(task.working_directory).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, static_cast<int>(task.window_style));
    if (task.run_as_user.has_value()) {
        sqlite3_bind_text(stmt, 8, ws2s(task.run_as_user->user_name).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, ws2s(task.run_as_user->domain).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, ws2s(task.run_as_user->encrypted_password).c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 8);
        sqlite3_bind_null(stmt, 9);
        sqlite3_bind_null(stmt, 10);
    }
    sqlite3_bind_int(stmt, 11, static_cast<int>(task.on_error));
    sqlite3_bind_int(stmt, 12, task.retry_count);
    sqlite3_bind_int(stmt, 13, static_cast<int>(task.on_overlap));
    if (task.timeout_minutes.has_value()) sqlite3_bind_int(stmt, 14, task.timeout_minutes.value());
    else sqlite3_bind_null(stmt, 14);
    sqlite3_bind_int(stmt, 15, task.kill_on_timeout ? 1 : 0);
    sqlite3_bind_int(stmt, 16, task.log_output ? 1 : 0);
    sqlite3_bind_int(stmt, 17, task.max_history_records);
    sqlite3_bind_text(stmt, 18, ws2s(task.modified_at).c_str(), -1, SQLITE_TRANSIENT);
    auto id_u8 = ws2s(task.id);
    sqlite3_bind_text(stmt, 19, id_u8.c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

void Database::delete_task(const std::wstring& id) {
    auto sql = "DELETE FROM tasks WHERE id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto id_u8 = ws2s(id);
    sqlite3_bind_text(stmt, 1, id_u8.c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

void Database::set_task_enabled(const std::wstring& id, bool enabled) {
    auto sql = "UPDATE tasks SET enabled=? WHERE id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    auto id_u8 = ws2s(id);
    sqlite3_bind_text(stmt, 2, id_u8.c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

// ---- TimeWindows ----

std::vector<TimeWindow> Database::get_time_windows(const std::wstring& task_id) {
    std::vector<TimeWindow> windows;
    auto sql = "SELECT * FROM time_windows WHERE task_id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto tid_u8 = ws2s(task_id);
    sqlite3_bind_text(stmt, 1, tid_u8.c_str(), -1, SQLITE_TRANSIENT);
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        check_step(db_, rc, stmt);
        TimeWindow w;
        w.id = s2ws(safe_col_text(stmt, 0));
        w.task_id = task_id;
        w.type = static_cast<TimeWindowType>(sqlite3_column_int(stmt, 2));
        auto* sd = sqlite3_column_text(stmt, 3);
        if (sd) w.start_date = s2ws((const char*)sd);
        auto* ed = sqlite3_column_text(stmt, 4);
        if (ed) w.end_date = s2ws((const char*)ed);
        // Parse JSON arrays for list fields
        auto* dow_str = sqlite3_column_text(stmt, 5);
        if (dow_str) {
            auto j = json::parse((const char*)dow_str);
            if (j.is_array()) for (auto& v : j.as_array()) w.days_of_week.push_back(v.as_int());
        }
        auto* dom_str = sqlite3_column_text(stmt, 6);
        if (dom_str) {
            auto j = json::parse((const char*)dom_str);
            if (j.is_array()) for (auto& v : j.as_array()) w.days_of_month.push_back(v.as_int());
        }
        auto* sd_str = sqlite3_column_text(stmt, 7);
        if (sd_str) {
            auto j = json::parse((const char*)sd_str);
            if (j.is_array()) for (auto& v : j.as_array()) w.specific_dates.push_back(s2ws(v.as_string()));
        }
        auto* et_str = sqlite3_column_text(stmt, 8);
        if (et_str) {
            auto j = json::parse((const char*)et_str);
            if (j.is_array()) for (auto& v : j.as_array()) w.exact_times.push_back(s2ws(v.as_string()));
        }
        auto* st = sqlite3_column_text(stmt, 9);
        if (st) w.start_time = s2ws((const char*)st);
        auto* et = sqlite3_column_text(stmt, 10);
        if (et) w.end_time = s2ws((const char*)et);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            w.repeat_interval_minutes = sqlite3_column_int(stmt, 11);
        auto* ru = sqlite3_column_text(stmt, 12);
        if (ru) w.repeat_until = s2ws((const char*)ru);
        windows.push_back(std::move(w));
    }
    sqlite3_finalize(stmt);
    return windows;
}

void Database::delete_time_windows(const std::wstring& task_id) {
    auto sql = "DELETE FROM time_windows WHERE task_id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto tid_u8 = ws2s(task_id);
    sqlite3_bind_text(stmt, 1, tid_u8.c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

void Database::insert_time_window(const TimeWindow& tw) {
    auto sql = "INSERT INTO time_windows (id,task_id,type,start_date,end_date,days_of_week,days_of_month,"
        "specific_dates,exact_times,start_time,end_time,repeat_interval_minutes,repeat_until) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);

    auto to_json_arr = [](const auto& vec, auto conv) -> std::string {
        json::Array arr;
        for (auto& v : vec) arr.push_back(conv(v));
        return json::serialize(arr);
    };

    auto int_conv = [](int v) -> json::Value { return json::Value(v); };
    auto str_conv = [this](const std::wstring& v) -> json::Value {
        return json::Value(ws2s(v));
    };

    sqlite3_bind_text(stmt, 1, ws2s(tw.id).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws2s(tw.task_id).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(tw.type));
    if (tw.start_date) sqlite3_bind_text(stmt, 4, ws2s(*tw.start_date).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 4);
    if (tw.end_date) sqlite3_bind_text(stmt, 5, ws2s(*tw.end_date).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 5);

    auto dow_json = to_json_arr(tw.days_of_week, int_conv);
    sqlite3_bind_text(stmt, 6, dow_json.c_str(), -1, SQLITE_TRANSIENT);
    auto dom_json = to_json_arr(tw.days_of_month, int_conv);
    sqlite3_bind_text(stmt, 7, dom_json.c_str(), -1, SQLITE_TRANSIENT);
    auto sd_json = to_json_arr(tw.specific_dates, str_conv);
    sqlite3_bind_text(stmt, 8, sd_json.c_str(), -1, SQLITE_TRANSIENT);
    auto et_json = to_json_arr(tw.exact_times, str_conv);
    sqlite3_bind_text(stmt, 9, et_json.c_str(), -1, SQLITE_TRANSIENT);

    if (tw.start_time) sqlite3_bind_text(stmt, 10, ws2s(*tw.start_time).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 10);
    if (tw.end_time) sqlite3_bind_text(stmt, 11, ws2s(*tw.end_time).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 11);
    if (tw.repeat_interval_minutes) sqlite3_bind_int(stmt, 12, *tw.repeat_interval_minutes);
    else sqlite3_bind_null(stmt, 12);
    if (tw.repeat_until) sqlite3_bind_text(stmt, 13, ws2s(*tw.repeat_until).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 13);

    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

// ---- RunHistory ----

void Database::insert_history(const RunHistory& h) {
    auto sql = "INSERT INTO run_history (id,task_id,start_time,end_time,exit_code,pid,status,error_message,output_path) "
        "VALUES (?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    sqlite3_bind_text(stmt, 1, ws2s(h.id).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws2s(h.task_id).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ws2s(h.start_time).c_str(), -1, SQLITE_TRANSIENT);
    if (h.end_time) sqlite3_bind_text(stmt, 4, ws2s(*h.end_time).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 4);
    if (h.exit_code) sqlite3_bind_int(stmt, 5, *h.exit_code);
    else sqlite3_bind_null(stmt, 5);
    sqlite3_bind_int(stmt, 6, h.pid);
    sqlite3_bind_int(stmt, 7, static_cast<int>(h.status));
    sqlite3_bind_text(stmt, 8, ws2s(h.error_message).c_str(), -1, SQLITE_TRANSIENT);
    if (h.output_path) sqlite3_bind_text(stmt, 9, ws2s(*h.output_path).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 9);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

void Database::update_history(const RunHistory& h) {
    auto sql = "UPDATE run_history SET end_time=?,exit_code=?,pid=?,status=?,error_message=?,output_path=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    if (h.end_time) sqlite3_bind_text(stmt, 1, ws2s(*h.end_time).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 1);
    if (h.exit_code) sqlite3_bind_int(stmt, 2, *h.exit_code);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_int(stmt, 3, h.pid);
    sqlite3_bind_int(stmt, 4, static_cast<int>(h.status));
    sqlite3_bind_text(stmt, 5, ws2s(h.error_message).c_str(), -1, SQLITE_TRANSIENT);
    if (h.output_path) sqlite3_bind_text(stmt, 6, ws2s(*h.output_path).c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 6);
    sqlite3_bind_text(stmt, 7, ws2s(h.id).c_str(), -1, SQLITE_TRANSIENT);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}

std::vector<RunHistory> Database::get_history(const std::wstring& task_id, int limit) {
    std::vector<RunHistory> history;
    auto sql = "SELECT * FROM run_history WHERE task_id=? COLLATE NOCASE ORDER BY start_time DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), stmt);
    auto tid_u8 = ws2s(task_id);
    sqlite3_bind_text(stmt, 1, tid_u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        check_step(db_, rc, stmt);
        RunHistory h;
        h.id = s2ws(safe_col_text(stmt, 0));
        h.task_id = task_id;
        h.start_time = s2ws(safe_col_text(stmt, 2));
        auto* et = sqlite3_column_text(stmt, 3);
        if (et) h.end_time = s2ws((const char*)et);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) h.exit_code = sqlite3_column_int(stmt, 4);
        h.pid = sqlite3_column_int(stmt, 5);
        h.status = static_cast<RunStatus>(sqlite3_column_int(stmt, 6));
        h.error_message = s2ws(safe_col_text(stmt, 7));
        auto* op = sqlite3_column_text(stmt, 8);
        if (op) h.output_path = s2ws((const char*)op);
        history.push_back(std::move(h));
    }
    sqlite3_finalize(stmt);
    return history;
}

void Database::trim_history(const std::wstring& task_id, int max_records) {
    auto count_sql = "SELECT COUNT(*) FROM run_history WHERE task_id=? COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    check_prepare(db_, sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr), stmt);
    auto tid_u8 = ws2s(task_id);
    sqlite3_bind_text(stmt, 1, tid_u8.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count <= max_records) return;

    auto del_sql = "DELETE FROM run_history WHERE id IN "
        "(SELECT id FROM run_history WHERE task_id=? ORDER BY start_time ASC LIMIT ?)";
    check_prepare(db_, sqlite3_prepare_v2(db_, del_sql, -1, &stmt, nullptr), stmt);
    sqlite3_bind_text(stmt, 1, tid_u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, count - max_records);
    check_step(db_, sqlite3_step(stmt), stmt);
    sqlite3_finalize(stmt);
}
