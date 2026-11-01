#pragma once
#include "sqlite3.h"
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Ошибка слоя хранения: сообщение SQLite с контекстом операции.
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& message) : std::runtime_error(message) {}
};

// Пул подготовленных выражений одного соединения.
// Ключ — текст SQL; освобождённые выражения переиспользуются без повторной
// подготовки. Все операции с картой защищены мьютексом, само выполнение
// выражения — нет: параллельное чтение в WAL разрешено контрактом.
class StatementPool {
public:
    explicit StatementPool(sqlite3* db);
    ~StatementPool();

    StatementPool(const StatementPool&) = delete;
    StatementPool& operator=(const StatementPool&) = delete;

    sqlite3_stmt* acquire(const std::string& sql);
    void release(const std::string& sql, sqlite3_stmt* stmt);

    // Финализирует все выражения. Вызывается перед закрытием соединения.
    void clear();

    sqlite3* db() const { return db_; }

private:
    sqlite3* db_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<sqlite3_stmt*>> free_;
};

// RAII-обёртка над выражением из пула: сбрасывает и возвращает его при разрушении.
class Statement {
public:
    Statement(StatementPool& pool, const std::string& sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;

    sqlite3_stmt* get() const { return stmt_; }

    void bind_text(int index, const std::string& value);
    void bind_int(int index, int value);
    void bind_null(int index);

    // true — получена строка (SQLITE_ROW); SQLITE_DONE даёт false.
    bool step_row();

    // Ожидает SQLITE_DONE; иначе бросает DbError.
    void step_done();

    int changes() const;

private:
    StatementPool& pool_;
    std::string sql_;
    sqlite3_stmt* stmt_;
};

// Повтор шага при SQLITE_BUSY/SQLITE_LOCKED: паузы 50/200/500 мс, не более 3 повторов.
int step_with_busy_retry(sqlite3_stmt* stmt);

// Выполнение SQL без результата (DDL, PRAGMA, BEGIN/COMMIT).
void exec_sql(sqlite3* db, const std::string& sql);

// Скалярные запросы для PRAGMA и служебных проверок.
int query_int(sqlite3* db, const std::string& sql);
std::string query_text(sqlite3* db, const std::string& sql);
