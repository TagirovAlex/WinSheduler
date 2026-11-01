#pragma once
#include "config.h"
#include "auth_policy.h"

// Аутентификация клиента именованного канала: impersonation, SID, членство
// в группе и Administrators. Контракт: ./docs/contracts/SECURITY.md раздел 2–3.
namespace auth {

// Локальная группа, членство в которой разрешает подключение к IPC.
inline constexpr const wchar_t* OPERATOR_GROUP_NAME = L"WinSheduler Users";

// SDDL для канала: SYSTEM и Administrators — полный доступ,
// группа операторов — GRGW (без FILE_CREATE_PIPE_INSTANCE).
// Если группа не найдена (установщик ещё не создал её), возвращает false,
// а sddl содержит только SYSTEM/Administrators.
bool build_pipe_sddl(std::wstring& sddl);

// Доступна ли группа операторов (реестр GroupSid или LookupAccountName).
// Если группы нет, канал доступен только SYSTEM/Administrators.
bool operator_group_available();

// Прогревает кэш SID группы при старте службы (без чтения реестра на соединение).
void warm_up_group_cache();

// ImpersonateNamedPipeClient + сбор идентичности. После успешного вызова
// поток остаётся impersonated, вызывающий обязан вызвать revert_to_self().
bool impersonate_client(HANDLE pipe, Identity& identity, std::wstring& error);
void revert_to_self();

// Дублирует primary-токен текущего (impersonated) потока.
// Используется планировщиком для запуска задачи от имени клиента (A-8).
HANDLE duplicate_client_token();

bool sid_to_string(PSID sid, std::wstring& out);

// Идентичность и токен текущего запроса (thread_local). Сервер выставляет
// их на время обработки соединения, обработчики читают.
const Identity* current_identity();
HANDLE current_client_token();
void set_current_context(const Identity& identity, HANDLE primary_token);
void clear_current_context();

} // namespace auth
