#pragma once
#include "config.h"
#include <string>

class Database;

// Тонкий адаптер планировщика над слоем хранения (трек C, схема v2).
// Планировщик не выполняет SQL напрямую: все операции идут через Database,
// поэтому writer-мьютекс и транзакции остаются в одном месте.
namespace dbcompat {

// Атомарно занимает слот (task_id, window_id, slot).
// true — слот свободен и отмечен; false — уже срабатывал или ошибка БД.
bool mark_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot, const std::wstring& fired_at_utc);

bool was_trigger_fired(Database& db, const std::wstring& task_id, const std::wstring& window_id,
    const std::wstring& slot);

// Удаляет состояние срабатываний задачи (при изменении или удалении задачи).
void clear_trigger_state(Database& db, const std::wstring& task_id);

// Удаляет слоты задачи старше before_slot (локальное "yyyy-MM-ddTHH:mm").
int prune_trigger_state(Database& db, const std::wstring& task_id, const std::wstring& before_slot);

// Вставляет запись истории; window_id передаётся отдельно (пусто для RunNow).
void insert_history(Database& db, const RunHistory& history, const std::wstring& window_id);

// Чистка файлов вывода по retention (DB.md, раздел 6).
int purge_expired_outputs(Database& db);

} // namespace dbcompat
