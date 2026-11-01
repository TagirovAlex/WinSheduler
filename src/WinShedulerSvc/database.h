#pragma once
#include "config.h"
#include "db_repository.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "sqlite3.h"

class StatementPool;
class TaskRepository;
class TimeWindowRepository;
class HistoryRepository;
class SettingsRepository;
class AuditRepository;
class TriggerStateRepository;

// Параметры открытия БД. Значения по умолчанию сохраняют поведение
// существующих вызовов Database::open(path).
struct DatabaseOptions {
    // Каталог файлов вывода (logs/output). Нужен для retention; пусто — не чистим.
    std::wstring output_dir;
    // Каталог резервных копий. Пусто — <каталог БД>/backup.
    std::wstring backup_dir;
    int output_retention_days = 30;
    int audit_retention_days = 180;
    // Требовать ACL «только SYSTEM/Administrators» на каталоге данных.
    bool require_secure_acl = false;
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::wstring& path, const DatabaseOptions& options = DatabaseOptions());
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Версия схемы (PRAGMA user_version). 0 — соединение не открыто.
    int schema_version() const;
    std::wstring path() const { return path_; }
    std::wstring last_error() const { return last_error_; }

    // Транзакция записи: BEGIN IMMEDIATE + RAII-rollback (DB.md, раздел 5).
    // Вложенные транзакции запрещены и приводят к DbError.
    class Transaction {
    public:
        explicit Transaction(Database& db);
        ~Transaction();

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        void commit();

    private:
        Database& db_;
        std::unique_lock<std::recursive_mutex> lock_;
        bool active_;
    };

    // Tasks
    std::vector<TaskDefinition> list_tasks();
    // Задачи указанного владельца; пустой SID — все задачи (администратор).
    std::vector<TaskDefinition> list_tasks_by_owner(const std::wstring& owner_sid);
    std::optional<TaskDefinition> get_task(const std::wstring& id);
    void insert_task(const TaskDefinition& task);
    void update_task(const TaskDefinition& task);
    void delete_task(const std::wstring& id);
    void set_task_enabled(const std::wstring& id, bool enabled);
    int task_count();

    // TimeWindows
    std::vector<TimeWindow> get_time_windows(const std::wstring& task_id);
    void delete_time_windows(const std::wstring& task_id);
    void insert_time_window(const TimeWindow& tw);
    // Замена набора триггеров целиком в одной транзакции (IPC.md, раздел 4).
    void replace_time_windows(const std::wstring& task_id, const std::vector<TimeWindow>& windows);

    // RunHistory
    void insert_history(const RunHistory& h, const std::wstring& window_id = std::wstring());
    void update_history(const RunHistory& h);
    std::vector<RunHistory> get_history(const std::wstring& task_id, int limit = 100);
    // История вместе с window_id (поля нет в RunHistory из config.h).
    std::vector<HistoryRecord> get_history_records(const std::wstring& task_id, int limit = 100);
    void trim_history(const std::wstring& task_id, int max_records);

    // Settings
    std::optional<std::wstring> get_setting(const std::wstring& key);
    int get_setting_int(const std::wstring& key, int fallback);
    void set_setting(const std::wstring& key, const std::wstring& value);
    std::vector<std::pair<std::wstring, std::wstring>> list_settings();

    // Audit
    void append_audit(AuditEntry& entry);
    std::vector<AuditEntry> list_audit(const std::wstring& from, const std::wstring& to, int limit = 500);

    // TriggerState
    bool mark_trigger_fired(const std::wstring& task_id, const std::wstring& window_id,
        const std::wstring& slot, const std::wstring& fired_at);
    std::optional<std::wstring> get_trigger_fired_at(
        const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot);
    std::vector<TriggerStateEntry> list_trigger_state(const std::wstring& task_id);
    int prune_trigger_state(const std::wstring& task_id, const std::wstring& before_slot);
    int clear_trigger_state(const std::wstring& task_id);

    // Retention (DB.md, раздел 6)
    int purge_expired_outputs();
    int purge_expired_audit();

    // Резервное копирование и экспорт (DB.md, раздел 7)
    bool backup_to(const std::wstring& path);
    bool export_history_csv(const std::wstring& task_id, const std::wstring& path);

    // Прямой доступ к соединению оставлен для существующего кода (scheduler/ipc).
    // Новый код должен использовать методы Database и Transaction.
    sqlite3* get_db() const { return db_; }

private:
    sqlite3* db_ = nullptr;
    std::wstring path_;
    std::wstring last_error_;
    DatabaseOptions options_;

    std::unique_ptr<StatementPool> pool_;
    std::unique_ptr<TaskRepository> tasks_;
    std::unique_ptr<TimeWindowRepository> windows_;
    std::unique_ptr<HistoryRepository> history_;
    std::unique_ptr<SettingsRepository> settings_;
    std::unique_ptr<AuditRepository> audit_;
    std::unique_ptr<TriggerStateRepository> trigger_state_;

    // Один writer: рекурсивный мьютекс допускает вызовы репозиториев внутри транзакции.
    std::recursive_mutex write_mutex_;
    int transaction_depth_ = 0;

    bool migrate();
    bool quick_check();
    std::wstring backup_file_path(int from_version) const;
    int effective_setting_int(const std::wstring& key, int fallback);
    void commit_transaction();
    void rollback_transaction();
};
