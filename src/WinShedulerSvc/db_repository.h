#pragma once
#include "config.h"
#include "db_stmt.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Репозитории схемы v2 (./docs/contracts/DB.md).
// Каждый репозиторий работает через пул подготовленных выражений одного
// соединения. Все запросы перечисляют колонки явно: SELECT * запрещён.

// Запись аудита (таблица audit_log).
struct AuditEntry {
    long long id = 0;
    std::wstring ts;
    std::wstring actor_sid;
    std::wstring action;
    std::wstring object_type;
    std::optional<std::wstring> object_id;
    std::wstring result;
    std::wstring details;
};

// Состояние срабатывания слота (таблица trigger_state).
struct TriggerStateEntry {
    std::wstring task_id;
    std::wstring window_id;
    std::wstring slot;
    std::wstring last_fired_at;
};

// История запуска вместе с идентификатором триггера. В RunHistory поля window_id нет.
struct HistoryRecord {
    RunHistory history;
    std::optional<std::wstring> window_id;
};

class TaskRepository {
public:
    explicit TaskRepository(StatementPool& pool);

    // owner_sid пустой — все задачи (администратор).
    std::vector<TaskDefinition> list(const std::wstring& owner_sid = std::wstring());
    std::optional<TaskDefinition> get(const std::wstring& id);
    void insert(const TaskDefinition& task);
    void update(const TaskDefinition& task);
    void remove(const std::wstring& id);
    void set_enabled(const std::wstring& id, bool enabled);
    int count();

    // Значение max_history_records задачи; nullopt — задача не найдена.
    std::optional<int> max_history_records(const std::wstring& task_id);

private:
    StatementPool& pool_;
};

class TimeWindowRepository {
public:
    explicit TimeWindowRepository(StatementPool& pool);

    std::vector<TimeWindow> list(const std::wstring& task_id);
    void insert(const TimeWindow& window);
    void remove_for_task(const std::wstring& task_id);

private:
    StatementPool& pool_;
};

class HistoryRepository {
public:
    explicit HistoryRepository(StatementPool& pool);

    // Вставка с автоматическим retention по tasks.max_history_records.
    void insert(const RunHistory& history, const std::wstring& window_id = std::wstring());
    void update(const RunHistory& history);
    std::vector<RunHistory> list(const std::wstring& task_id, int limit);
    std::vector<HistoryRecord> list_records(const std::wstring& task_id, int limit);
    // Оставить не более max_records самых свежих записей задачи.
    int trim(const std::wstring& task_id, int max_records);

private:
    StatementPool& pool_;
};

class SettingsRepository {
public:
    explicit SettingsRepository(StatementPool& pool);

    std::optional<std::wstring> get(const std::wstring& key);
    int get_int(const std::wstring& key, int fallback);
    void set(const std::wstring& key, const std::wstring& value);
    std::vector<std::pair<std::wstring, std::wstring>> list();

private:
    StatementPool& pool_;
};

class AuditRepository {
public:
    explicit AuditRepository(StatementPool& pool);

    // ts заполняется текущим UTC, если не задан.
    void append(AuditEntry& entry);
    // from/to — UTC ISO 8601; пустая строка означает отсутствие границы.
    std::vector<AuditEntry> list(const std::wstring& from, const std::wstring& to, int limit);
    // Удалить записи старше ts. Возвращает число удалённых.
    int purge_before(const std::wstring& ts);
    long long count();

private:
    StatementPool& pool_;
};

class TriggerStateRepository {
public:
    explicit TriggerStateRepository(StatementPool& pool);

    // true — слот отмечен впервые; false — уже срабатывал (повтор запрещён).
    bool mark_fired(const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot,
        const std::wstring& fired_at);
    std::optional<std::wstring> get_fired_at(
        const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot);
    std::vector<TriggerStateEntry> list(const std::wstring& task_id);
    int prune_before(const std::wstring& task_id, const std::wstring& slot);
    int remove_for_task(const std::wstring& task_id);

private:
    StatementPool& pool_;
};
