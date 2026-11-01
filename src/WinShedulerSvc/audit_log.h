#pragma once
#include <string>
#include "database.h"

// Совместимость с треком C: после появления db_repository.h аудит пишется
// через Database::append_audit, иначе — прямым параметризованным INSERT.
#if defined(__has_include)
#  if __has_include("db_repository.h")
#    include "db_repository.h"
#    define WINSHEDULER_DB_REPOSITORY 1
#  endif
#endif

// Аудит действий IPC. Контракт: ./docs/contracts/SECURITY.md раздел 6,
// схема таблицы — ./docs/contracts/DB.md (audit_log).
namespace audit {

enum class Result {
    Ok,
    Denied,
    Error
};

const wchar_t* result_name(Result result);

// Запись аудита. При наличии репозитория трека C используется append_audit;
// иначе — INSERT в audit_log, а при отсутствии таблицы (схема v2 ещё не
// установлена) — файл ./logs/audit.log, чтобы событие не потерялось.
void write(const std::wstring& log_dir, Database& db,
           const std::wstring& actor_sid,
           const std::wstring& action,
           const std::wstring& object_type,
           const std::wstring& object_id,
           Result result,
           const std::wstring& details);

// Убирает переводы строк и табуляцию (защита от log injection).
std::wstring redact(const std::wstring& details);

} // namespace audit
