#include "db_compat.h"
#include "database.h"

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

int prune_trigger_state(Database& db, const std::wstring& task_id, const std::wstring& before_slot) {
    return db.prune_trigger_state(task_id, before_slot);
}

void insert_history(Database& db, const RunHistory& history, const std::wstring& window_id) {
    db.insert_history(history, window_id);
}

int purge_expired_outputs(Database& db) {
    return db.purge_expired_outputs();
}

} // namespace dbcompat
