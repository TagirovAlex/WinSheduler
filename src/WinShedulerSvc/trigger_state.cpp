#include "trigger_state.h"
#include "database.h"
#include "db_compat.h"

namespace {

// Разделитель, который не встречается ни в GUID, ни в слоте.
constexpr wchar_t kKeySeparator = L'\x1f';

} // namespace

std::wstring MemoryTriggerStateStore::key(const std::wstring& task_id,
    const std::wstring& window_id, const std::wstring& slot) {
    return task_id + kKeySeparator + window_id + kKeySeparator + slot;
}

bool MemoryTriggerStateStore::was_fired(const std::wstring& task_id,
    const std::wstring& window_id, const std::wstring& slot) {
    std::lock_guard<std::mutex> guard(mutex_);
    return fired_.find(key(task_id, window_id, slot)) != fired_.end();
}

bool MemoryTriggerStateStore::try_mark_fired(const std::wstring& task_id,
    const std::wstring& window_id, const std::wstring& slot, const std::wstring&) {
    std::lock_guard<std::mutex> guard(mutex_);
    return fired_.insert(key(task_id, window_id, slot)).second;
}

void MemoryTriggerStateStore::forget_task(const std::wstring& task_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    const std::wstring prefix = task_id + kKeySeparator;
    for (auto it = fired_.begin(); it != fired_.end();) {
        if (it->rfind(prefix, 0) == 0) {
            it = fired_.erase(it);
        } else {
            ++it;
        }
    }
}

DatabaseTriggerStateStore::DatabaseTriggerStateStore(Database& db) : db_(db) {}

bool DatabaseTriggerStateStore::was_fired(const std::wstring& task_id,
    const std::wstring& window_id, const std::wstring& slot) {
    return dbcompat::was_trigger_fired(db_, task_id, window_id, slot);
}

bool DatabaseTriggerStateStore::try_mark_fired(const std::wstring& task_id,
    const std::wstring& window_id, const std::wstring& slot, const std::wstring& fired_at_utc) {
    return dbcompat::mark_trigger_fired(db_, task_id, window_id, slot, fired_at_utc);
}

void DatabaseTriggerStateStore::forget_task(const std::wstring& task_id) {
    dbcompat::clear_trigger_state(db_, task_id);
}
