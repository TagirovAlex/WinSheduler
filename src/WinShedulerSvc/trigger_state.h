#pragma once
#include "config.h"
#include <mutex>
#include <string>
#include <unordered_set>

class Database;

// Состояние срабатываний: (task_id, window_id, slot) → last_fired_at.
// Контракт ./docs/contracts/DB.md, таблица trigger_state.
// Слот — локальное "yyyy-MM-ddTHH:mm"; повторный запуск того же слота запрещён.
class ITriggerStateStore {
public:
    virtual ~ITriggerStateStore() = default;

    virtual bool was_fired(const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot) = 0;

    // Атомарно фиксирует слот. false — слот уже был зафиксирован (гонка или повтор).
    virtual bool try_mark_fired(const std::wstring& task_id, const std::wstring& window_id,
        const std::wstring& slot, const std::wstring& fired_at_utc) = 0;

    // Удаляет состояние задачи (при удалении задачи или смене расписания).
    virtual void forget_task(const std::wstring& task_id) = 0;
};

// Реализация в памяти: для тестов и для работы без БД.
class MemoryTriggerStateStore final : public ITriggerStateStore {
public:
    bool was_fired(const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot) override;
    bool try_mark_fired(const std::wstring& task_id, const std::wstring& window_id,
        const std::wstring& slot, const std::wstring& fired_at_utc) override;
    void forget_task(const std::wstring& task_id) override;

private:
    static std::wstring key(const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot);
    std::mutex mutex_;
    std::unordered_set<std::wstring> fired_;
};

// Реализация поверх слоя хранения (трек C, trigger_state).
class DatabaseTriggerStateStore final : public ITriggerStateStore {
public:
    explicit DatabaseTriggerStateStore(Database& db);

    bool was_fired(const std::wstring& task_id, const std::wstring& window_id, const std::wstring& slot) override;
    bool try_mark_fired(const std::wstring& task_id, const std::wstring& window_id,
        const std::wstring& slot, const std::wstring& fired_at_utc) override;
    void forget_task(const std::wstring& task_id) override;

private:
    Database& db_;
};
