#pragma once
#include "config.h"
#include "database.h"
#include <memory>
#include <mutex>
#include <vector>

// Кэш расписания в памяти. Тик планировщика не должен читать БД на каждую задачу:
// снимок перечитывается только после инвалидации (CRUD, toggle, RunNow).
class ScheduleCache {
public:
    struct Entry {
        TaskDefinition task;
        std::vector<TimeWindow> windows;
    };

    explicit ScheduleCache(Database& db);

    // Актуальный снимок. При инвалидации перечитывает задачи и их окна.
    std::shared_ptr<const std::vector<Entry>> get();

    // Инвалидация после изменения задач (CreateTask/UpdateTask/DeleteTask/SetTaskEnabled).
    void invalidate();

    // Количество перечитываний БД: используется в тестах и диагностике.
    int rebuild_count() const;

private:
    void rebuild();

    Database& db_;
    mutable std::mutex mutex_;
    std::shared_ptr<const std::vector<Entry>> data_;
    bool dirty_ = true;
    int rebuilds_ = 0;
};
