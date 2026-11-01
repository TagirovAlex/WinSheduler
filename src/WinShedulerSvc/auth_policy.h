#pragma once
#include <string>

// Политика доступа IPC. Чистый модуль без Win32-зависимостей, чтобы его
// можно было покрыть юнит-тестами (./tests/ipc).
// Матрица прав: ./docs/contracts/SECURITY.md раздел 2, ./docs/contracts/IPC.md раздел 3.
namespace auth {

// Действия протокола. Имена совпадают с контрактом IPC.md.
enum class Action {
    Ping,
    GetStatus,
    GetDashboard,
    ListTasks,
    GetTask,
    GetTimeWindows,
    GetHistory,
    CreateTask,
    UpdateTask,
    DeleteTask,
    SetTaskEnabled,
    RunNow,
    GetAudit,
    GetSettings,
    SetSettings,
    Unknown
};

// Имя действия из запроса (регистр учитывается, как в контракте).
Action action_from_name(const std::wstring& name);

// Идентичность клиента, полученная из impersonated-токена канала.
struct Identity {
    std::wstring sid;          // SID вызывающего, строковая форма
    bool is_admin = false;     // член Administrators (elevated)
    bool is_operator = false;  // член группы WinSheduler Users
    bool authenticated = false;

    bool privileged() const { return is_admin; }
};

// Сравнение SID без учёта регистра (S-1-5-18 == s-1-5-18).
bool equal_sid(const std::wstring& a, const std::wstring& b);

class Policy {
public:
    // Разрешено ли действие идентичности (без учёта владельца объекта).
    static bool allows(const Identity& id, Action action);

    // Может ли вызывающий видеть и менять задачу указанного владельца.
    // Пустой owner_sid — legacy-задача (по контракту DB.md владелец S-1-5-18),
    // доступна только администратору.
    static bool can_access_task(const Identity& id, const std::wstring& owner_sid);

    // Привилегированные поля и режимы: RunAsUser/RunAsDomain/Password,
    // задача от SYSTEM, SessionMode=ActiveUser.
    static bool can_use_credentials(const Identity& id) { return id.is_admin; }
    static bool can_run_as_system(const Identity& id) { return id.is_admin; }
    static bool can_use_active_user(const Identity& id) { return id.is_admin; }

    // Действия, доступные только администратору.
    static bool is_privileged(Action action);

    // Действия, изменяющие состояние (для аудита).
    static bool is_mutation(Action action);
};

} // namespace auth
