#include "db_stmt.h"

#include <windows.h>

namespace {

[[noreturn]] void throw_sqlite(sqlite3* db, const char* operation) {
    std::string message = operation ? operation : "SQLite";
    if (db) {
        message += ": ";
        message += sqlite3_errmsg(db);
    }
    throw DbError(message);
}

} // namespace

StatementPool::StatementPool(sqlite3* db) : db_(db) {}

StatementPool::~StatementPool() { clear(); }

sqlite3_stmt* StatementPool::acquire(const std::string& sql) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = free_.find(sql);
        if (it != free_.end() && !it->second.empty()) {
            sqlite3_stmt* stmt = it->second.back();
            it->second.pop_back();
            return stmt;
        }
    }

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        throw_sqlite(db_, "Не удалось подготовить SQL");
    }
    return stmt;
}

void StatementPool::release(const std::string& sql, sqlite3_stmt* stmt) {
    if (!stmt) return;
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    std::lock_guard<std::mutex> lock(mutex_);
    free_[sql].push_back(stmt);
}

void StatementPool::clear() {
    std::unordered_map<std::string, std::vector<sqlite3_stmt*>> local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local.swap(free_);
    }
    for (auto& [sql, statements] : local) {
        (void)sql;
        for (sqlite3_stmt* stmt : statements) sqlite3_finalize(stmt);
    }
}

Statement::Statement(StatementPool& pool, const std::string& sql)
    : pool_(pool), sql_(sql), stmt_(pool.acquire(sql)) {}

Statement::~Statement() { pool_.release(sql_, stmt_); }

void Statement::bind_text(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw_sqlite(pool_.db(), "Не удалось привязать текст");
    }
}

void Statement::bind_int(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
        throw_sqlite(pool_.db(), "Не удалось привязать число");
    }
}

void Statement::bind_null(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        throw_sqlite(pool_.db(), "Не удалось привязать NULL");
    }
}

bool Statement::step_row() {
    const int rc = step_with_busy_retry(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw_sqlite(pool_.db(), "Ошибка выполнения SQL");
}

void Statement::step_done() {
    const int rc = step_with_busy_retry(stmt_);
    if (rc != SQLITE_DONE) throw_sqlite(pool_.db(), "Ожидалось завершение SQL");
}

int Statement::changes() const { return sqlite3_changes(pool_.db()); }

int step_with_busy_retry(sqlite3_stmt* stmt) {
    static const DWORD delays[] = { 50, 200, 500 };
    int rc = sqlite3_step(stmt);
    for (DWORD delay : delays) {
        if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
        Sleep(delay);
        rc = sqlite3_step(stmt);
    }
    return rc;
}

void exec_sql(sqlite3* db, const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "неизвестная ошибка";
        sqlite3_free(error);
        throw DbError("SQLite: " + message);
    }
}

int query_int(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw_sqlite(db, "Не удалось подготовить SQL");
    }
    int value = 0;
    if (step_with_busy_retry(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

std::string query_text(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw_sqlite(db, "Не удалось подготовить SQL");
    }
    std::string value;
    if (step_with_busy_retry(stmt) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(stmt, 0);
        if (text) value = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return value;
}
