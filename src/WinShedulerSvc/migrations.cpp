#include "migrations.h"
#include "db_stmt.h"

#include <string>
#include <vector>

namespace migrations {
namespace {

// Схема v2. Легаси-колонки (time_windows.type) сохраняются для совместимости:
// контракт DB.md, раздел 4, запрещает удалять их в v2.
const char* kSchemaV2 = R"SQL(
CREATE TABLE IF NOT EXISTS tasks (
    id TEXT PRIMARY KEY,
    owner_sid TEXT NOT NULL DEFAULT 'S-1-5-18',
    name TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL DEFAULT '',
    enabled INTEGER NOT NULL DEFAULT 1,
    program_path TEXT NOT NULL DEFAULT '',
    arguments TEXT NOT NULL DEFAULT '',
    working_directory TEXT NOT NULL DEFAULT '',
    window_style INTEGER NOT NULL DEFAULT 1,
    session_mode INTEGER NOT NULL DEFAULT 0,
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
    kind INTEGER NOT NULL DEFAULT 1,
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
    day_interval INTEGER NOT NULL DEFAULT 1,
    week_interval INTEGER NOT NULL DEFAULT 1,
    month_interval INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS trigger_state (
    task_id TEXT NOT NULL,
    window_id TEXT NOT NULL,
    slot TEXT NOT NULL,
    last_fired_at TEXT NOT NULL,
    PRIMARY KEY (task_id, window_id, slot)
);

CREATE TABLE IF NOT EXISTS run_history (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    window_id TEXT,
    start_time TEXT NOT NULL DEFAULT '',
    end_time TEXT,
    exit_code INTEGER,
    pid INTEGER NOT NULL DEFAULT 0,
    status INTEGER NOT NULL DEFAULT 0,
    error_message TEXT NOT NULL DEFAULT '',
    output_path TEXT,
    triggered_by INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL,
    actor_sid TEXT NOT NULL,
    action TEXT NOT NULL,
    object_type TEXT NOT NULL,
    object_id TEXT,
    result TEXT NOT NULL,
    details TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_tasks_owner ON tasks(owner_sid);
CREATE INDEX IF NOT EXISTS idx_tasks_enabled ON tasks(enabled);
CREATE INDEX IF NOT EXISTS idx_time_windows_task ON time_windows(task_id);
CREATE INDEX IF NOT EXISTS idx_trigger_state_task ON trigger_state(task_id);
CREATE INDEX IF NOT EXISTS idx_run_history_task_start ON run_history(task_id, start_time DESC);
CREATE INDEX IF NOT EXISTS idx_run_history_status ON run_history(status);
CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(ts DESC);
)SQL";

void add_column_if_missing(sqlite3* db, const char* table, const char* column, const char* definition) {
    if (!table_exists(db, table)) return; // таблицу создаст create_schema_v2
    if (column_exists(db, table, column)) return;
    exec_sql(db, std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + definition);
}

// Заполнение kind по правилам контракта DB.md, раздел 4, пункт 4.
const char* kFillKind = R"SQL(
UPDATE time_windows SET kind = CASE
    WHEN type = 1 THEN 4
    WHEN type = 0 AND days_of_month IS NOT NULL AND days_of_month NOT IN ('', '[]') THEN 3
    WHEN type = 0 AND days_of_week IS NOT NULL AND days_of_week NOT IN ('', '[]') THEN 2
    ELSE 1
END
)SQL";

} // namespace

bool table_exists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        throw DbError(std::string("Не удалось проверить таблицу: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("Не удалось прочитать схему: ") + sqlite3_errmsg(db));
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = sqlite3_column_text(stmt, 1);
        if (name && column && std::string(reinterpret_cast<const char*>(name)) == column) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int read_user_version(sqlite3* db) { return query_int(db, "PRAGMA user_version"); }

void set_user_version(sqlite3* db, int version) {
    exec_sql(db, "PRAGMA user_version = " + std::to_string(version));
}

void create_schema_v2(sqlite3* db) { exec_sql(db, kSchemaV2); }

void migrate_v1_to_v2(sqlite3* db) {
    // 1. Новые колонки tasks.
    add_column_if_missing(db, "tasks", "owner_sid", "TEXT NOT NULL DEFAULT 'S-1-5-18'");
    add_column_if_missing(db, "tasks", "session_mode", "INTEGER NOT NULL DEFAULT 0");

    // 2. Новые колонки time_windows.
    add_column_if_missing(db, "time_windows", "kind", "INTEGER NOT NULL DEFAULT 1");
    add_column_if_missing(db, "time_windows", "day_interval", "INTEGER NOT NULL DEFAULT 1");
    add_column_if_missing(db, "time_windows", "week_interval", "INTEGER NOT NULL DEFAULT 1");
    add_column_if_missing(db, "time_windows", "month_interval", "INTEGER NOT NULL DEFAULT 1");

    // 3. Новые колонки run_history (нужны целевой схеме v2).
    add_column_if_missing(db, "run_history", "window_id", "TEXT");
    add_column_if_missing(db, "run_history", "triggered_by", "INTEGER NOT NULL DEFAULT 0");

    // 4. Заполнение kind для существующих окон.
    exec_sql(db, kFillKind);

    // 5. Владелец legacy-задач: SID из install.owner_sid либо S-1-5-18.
    const std::string owner = legacy_owner_sid(db);
    if (owner != DB_LEGACY_OWNER_SID) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE tasks SET owner_sid=?1", -1, &stmt, nullptr) != SQLITE_OK) {
            throw DbError(std::string("Не удалось заполнить owner_sid: ") + sqlite3_errmsg(db));
        }
        sqlite3_bind_text(stmt, 1, owner.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            throw DbError(std::string("Не удалось заполнить owner_sid: ") + sqlite3_errmsg(db));
        }
    }

    // 6. Новые таблицы и индексы. CREATE ... IF NOT EXISTS не трогает данные.
    create_schema_v2(db);
}

std::string legacy_owner_sid(sqlite3* db) {
    if (!table_exists(db, "settings")) return DB_LEGACY_OWNER_SID;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key='install.owner_sid'", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return DB_LEGACY_OWNER_SID;
    }
    std::string value = DB_LEGACY_OWNER_SID;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(stmt, 0);
        if (text) {
            const std::string candidate(reinterpret_cast<const char*>(text));
            if (!candidate.empty()) value = candidate;
        }
    }
    sqlite3_finalize(stmt);
    return value;
}

} // namespace migrations
