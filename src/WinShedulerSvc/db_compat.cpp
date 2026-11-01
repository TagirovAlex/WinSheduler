#include "db_compat.h"
#include "database.h"

#if WS_DB_SCHEMA_V2

namespace dbcompat {

bool mark_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at_utc) {
    return db.mark_trigger_fired(task_id, window_id, slot, fired_at_utc);
}

bool was_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot) {
    return db.get_trigger_fired_at(task_id, window_id, slot).has_value();
}

void clear_trigger_state(Database& db, const std::wstring& task_id) {
    db.clear_trigger_state(task_id);
}

void insert_history(Database& db, const RunHistory& history, const std::wstring& window_id) {
    db.insert_history(history, window_id);
}

int purge_expired_outputs(Database& db) {
    return db.purge_expired_outputs();
}

} // namespace dbcompat

#else // WS_DB_SCHEMA_V2

#include "sqlite3.h"

namespace {

bool prepare(Database& db, const char* sql, sqlite3_stmt** statement) {
    return sqlite3_prepare_v2(db.get_db(), sql, -1, statement, nullptr) == SQLITE_OK;
}

void bind_text(sqlite3_stmt* statement, int index, const std::wstring& value) {
    const std::string utf8 = ws2u(value);
    sqlite3_bind_text(statement, index, utf8.c_str(), -1, SQLITE_TRANSIENT);
}

// Схема trigger_state совпадает с ./docs/contracts/DB.md; IF NOT EXISTS
// не конфликтует с миграцией трека C.
bool ensure_schema(Database& db) {
    static std::once_flag once;
    static bool created = false;
    std::call_once(once, [&db]() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS trigger_state ("
            " task_id TEXT NOT NULL,"
            " window_id TEXT NOT NULL,"
            " slot TEXT NOT NULL,"
            " last_fired_at TEXT NOT NULL,"
            " PRIMARY KEY (task_id, window_id, slot));"
            "CREATE INDEX IF NOT EXISTS idx_trigger_state_task ON trigger_state(task_id);";
        created = db.get_db() != nullptr && sqlite3_exec(db.get_db(), sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    });
    return created;
}

} // namespace

namespace dbcompat {

bool mark_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at_utc) {
    if (!ensure_schema(db)) return false;

    sqlite3_stmt* statement = nullptr;
    if (!prepare(db, "INSERT OR IGNORE INTO trigger_state (task_id, window_id, slot, last_fired_at) VALUES (?,?,?,?)", &statement)) {
        return false;
    }
    bind_text(statement, 1, task_id);
    bind_text(statement, 2, window_id);
    bind_text(statement, 3, slot);
    bind_text(statement, 4, fired_at_utc);

    const bool inserted = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db.get_db()) > 0;
    sqlite3_finalize(statement);
    return inserted;
}

bool was_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot) {
    if (!ensure_schema(db)) return true; // ошибка БД: не запускаем повторно

    sqlite3_stmt* statement = nullptr;
    if (!prepare(db, "SELECT 1 FROM trigger_state WHERE task_id=? AND window_id=? AND slot=? LIMIT 1", &statement)) {
        return true;
    }
    bind_text(statement, 1, task_id);
    bind_text(statement, 2, window_id);
    bind_text(statement, 3, slot);
    const bool fired = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return fired;
}

void clear_trigger_state(Database& db, const std::wstring& task_id) {
    if (!ensure_schema(db)) return;
    sqlite3_stmt* statement = nullptr;
    if (!prepare(db, "DELETE FROM trigger_state WHERE task_id=?", &statement)) return;
    bind_text(statement, 1, task_id);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
}

void insert_history(Database& db, const RunHistory& history, const std::wstring& window_id) {
    (void)window_id; // в схеме v1 колонки window_id нет
    db.insert_history(history);
}

int purge_expired_outputs(Database& db) {
    (void)db; // retention файлов вывода появится вместе со схемой v2
    return 0;
}

} // namespace dbcompat

#endif // WS_DB_SCHEMA_V2
