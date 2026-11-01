#pragma once
#include "config.h"
#include <string>

class Database;

// Совместимость планировщика с двумя состояниями слоя хранения:
//  - схема v2 (трек C): Database::mark_trigger_fired / insert_history(h, window_id)
//    / purge_expired_outputs;
//  - текущее дерево до слияния ветки C (v1): запасной путь поверх того же SQLite.
// После слияния ветки C запасной путь удаляется вместе с этим файлом.
#if __has_include("db_repository.h")
#define WS_DB_SCHEMA_V2 1
#else
#define WS_DB_SCHEMA_V2 0
#endif

namespace dbcompat {

// Атомарно занимает слот (task_id, window_id, slot).
// true — слот свободен и отмечен; false — уже срабатывал или ошибка БД.
bool mark_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at_utc);

bool was_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot);

// Удаляет состояние срабатываний задачи (при изменении или удалении задачи).
void clear_trigger_state(Database& db, const std::wstring& task_id);

// Вставляет запись истории; window_id передаётся отдельно (NULL для RunNow).
void insert_history(Database& db, const RunHistory& history, const std::wstring& window_id);

// Чистка файлов вывода по retention. 0 — если слой хранения не поддерживает.
int purge_expired_outputs(Database& db);

} // namespace dbcompat
