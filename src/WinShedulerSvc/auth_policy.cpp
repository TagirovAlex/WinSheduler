#include "auth_policy.h"
#include <cwctype>

namespace auth {

namespace {

// Сравнение SID без учёта регистра.
bool iequals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

} // namespace

bool equal_sid(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    return iequals(a, b);
}

Action action_from_name(const std::wstring& name) {
    if (name == L"Ping") return Action::Ping;
    if (name == L"GetStatus") return Action::GetStatus;
    if (name == L"GetDashboard") return Action::GetDashboard;
    if (name == L"ListTasks") return Action::ListTasks;
    if (name == L"GetTask") return Action::GetTask;
    if (name == L"GetTimeWindows") return Action::GetTimeWindows;
    if (name == L"GetHistory") return Action::GetHistory;
    if (name == L"CreateTask") return Action::CreateTask;
    if (name == L"UpdateTask") return Action::UpdateTask;
    if (name == L"DeleteTask") return Action::DeleteTask;
    if (name == L"SetTaskEnabled") return Action::SetTaskEnabled;
    // Переходный режим: старые имена действий (IPC.md раздел 7).
    if (name == L"EnableTask") return Action::SetTaskEnabled;
    if (name == L"DisableTask") return Action::SetTaskEnabled;
    if (name == L"RunNow") return Action::RunNow;
    if (name == L"StopTask") return Action::StopTask;
    if (name == L"GetAudit") return Action::GetAudit;
    if (name == L"GetSettings") return Action::GetSettings;
    if (name == L"SetSettings") return Action::SetSettings;
    return Action::Unknown;
}

bool Policy::is_privileged(Action action) {
    switch (action) {
    case Action::GetAudit:
    case Action::GetSettings:
    case Action::SetSettings:
        return true;
    default:
        return false;
    }
}

bool Policy::is_mutation(Action action) {
    switch (action) {
    case Action::CreateTask:
    case Action::UpdateTask:
    case Action::DeleteTask:
    case Action::SetTaskEnabled:
    case Action::RunNow:
    case Action::StopTask:
    case Action::SetSettings:
        return true;
    default:
        return false;
    }
}

bool Policy::allows(const Identity& id, Action action) {
    // Ping разрешён любому подключившемуся клиенту (контракт IPC.md, таблица прав).
    if (action == Action::Ping) return true;
    if (!id.authenticated) return false;

    // Не-оператор (гость) не получает ничего, кроме Ping.
    if (!id.is_operator && !id.is_admin) return false;

    if (is_privileged(action)) return id.is_admin;

    switch (action) {
    case Action::GetStatus:
    case Action::GetDashboard:
    case Action::ListTasks:
    case Action::GetTask:
    case Action::GetTimeWindows:
    case Action::GetHistory:
    case Action::CreateTask:
    case Action::UpdateTask:
    case Action::DeleteTask:
    case Action::SetTaskEnabled:
    case Action::RunNow:
    case Action::StopTask:
        return true;
    case Action::Unknown:
        return false;
    default:
        return false;
    }
}

bool Policy::can_access_task(const Identity& id, const std::wstring& owner_sid) {
    if (!id.authenticated) return false;
    if (id.is_admin) return true;
    if (!id.is_operator) return false;
    // Legacy-задача без владельца и задача SYSTEM доступны только администратору.
    if (owner_sid.empty()) return false;
    if (equal_sid(owner_sid, L"S-1-5-18")) return false;
    return equal_sid(owner_sid, id.sid);
}

} // namespace auth
