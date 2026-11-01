#include "schedule_cache.h"

ScheduleCache::ScheduleCache(Database& db) : db_(db) {}

std::shared_ptr<const std::vector<ScheduleCache::Entry>> ScheduleCache::get() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!data_ || dirty_) rebuild();
    return data_;
}

void ScheduleCache::invalidate() {
    std::lock_guard<std::mutex> guard(mutex_);
    dirty_ = true;
}

int ScheduleCache::rebuild_count() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return rebuilds_;
}

void ScheduleCache::rebuild() {
    auto entries = std::make_shared<std::vector<Entry>>();
    auto tasks = db_.list_tasks();
    entries->reserve(tasks.size());
    for (auto& task : tasks) {
        Entry entry;
        entry.task = task;
        entry.windows = db_.get_time_windows(task.id);
        entries->push_back(std::move(entry));
    }
    data_ = entries;
    dirty_ = false;
    ++rebuilds_;
}
