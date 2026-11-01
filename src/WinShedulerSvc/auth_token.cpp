#include "auth_token.h"
#include <sddl.h>
#include <vector>
#include <mutex>

#pragma comment(lib, "advapi32.lib")

namespace auth {

namespace {

// Кэш SID группы операторов: читается один раз при старте службы.
// Источники по приоритету: реестр (пишет установщик, F-5), затем LookupAccountName.
struct OperatorGroup {
    bool initialized = false;
    bool available = false;
    std::wstring sid_string;
    std::vector<BYTE> sid;
};

OperatorGroup& operator_group() {
    static OperatorGroup group;
    return group;
}

std::mutex& operator_group_mutex() {
    static std::mutex m;
    return m;
}

struct ThreadContext {
    Identity identity;
    HANDLE primary_token = nullptr;
};

ThreadContext& context() {
    static thread_local ThreadContext ctx;
    return ctx;
}

bool token_user_sid(HANDLE token, std::wstring& sid_out) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) return false;
    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) return false;
    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    return sid_to_string(user->User.Sid, sid_out);
}

// SID локальной группы по имени.
bool lookup_group_sid(const wchar_t* name, std::vector<BYTE>& sid_buffer, std::wstring& sid_string) {
    DWORD sid_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE use = SidTypeUnknown;

    LookupAccountNameW(nullptr, name, nullptr, &sid_size, nullptr, &domain_size, &use);
    if (sid_size == 0) return false;

    sid_buffer.resize(sid_size);
    std::wstring domain(domain_size > 0 ? domain_size : 1, L'\0');
    if (!LookupAccountNameW(nullptr, name, sid_buffer.data(), &sid_size,
                            domain.data(), &domain_size, &use)) {
        return false;
    }
    if (use != SidTypeGroup && use != SidTypeAlias) return false;
    return sid_to_string(sid_buffer.data(), sid_string);
}

// SID группы из HKLM/SOFTWARE/WinSheduler, значение GroupSid (REG_SZ).
std::wstring registry_group_sid() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinSheduler", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buffer[256] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer) - sizeof(wchar_t));
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, L"GroupSid", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return {};
    return std::wstring(buffer, size / sizeof(wchar_t));
}

bool resolve_operator_group() {
    std::lock_guard<std::mutex> lock(operator_group_mutex());
    OperatorGroup& group = operator_group();
    if (group.initialized) return group.available;
    group.initialized = true;

    const std::wstring from_registry = registry_group_sid();
    if (!from_registry.empty()) {
        PSID parsed = nullptr;
        if (ConvertStringSidToSidW(from_registry.c_str(), &parsed)) {
            const DWORD length = GetLengthSid(parsed);
            group.sid.resize(length);
            CopySid(length, group.sid.data(), parsed);
            LocalFree(parsed);
            group.sid_string = from_registry;
            group.available = true;
            return true;
        }
    }

    std::vector<BYTE> buffer;
    std::wstring resolved;
    if (lookup_group_sid(OPERATOR_GROUP_NAME, buffer, resolved)) {
        group.sid = buffer;
        group.sid_string = resolved;
        group.available = true;
        return true;
    }

    group.available = false;
    return false;
}

// CheckTokenMembership с NULL-токеном проверяет impersonated-токен потока.
bool check_membership(PSID sid, bool& is_member) {
    BOOL member = FALSE;
    if (!CheckTokenMembership(nullptr, sid, &member)) return false;
    is_member = member != FALSE;
    return true;
}

bool is_builtin_admin() {
    BYTE buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD size = sizeof(buffer);
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, buffer, &size)) return false;
    bool member = false;
    if (!check_membership(buffer, member)) return false;
    return member;
}

bool duplicate_current_thread_token() {
    HANDLE thread_token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE,
                         TRUE, &thread_token)) {
        return false;
    }
    HANDLE primary = nullptr;
    BOOL ok = DuplicateTokenEx(thread_token, TOKEN_ALL_ACCESS, nullptr,
                               SecurityImpersonation, TokenPrimary, &primary);
    CloseHandle(thread_token);
    if (!ok) return false;
    context().primary_token = primary;
    return true;
}

} // namespace

bool sid_to_string(PSID sid, std::wstring& out) {
    if (sid == nullptr) return false;
    LPWSTR str = nullptr;
    if (!ConvertSidToStringSidW(sid, &str)) return false;
    out.assign(str);
    LocalFree(str);
    return true;
}

bool build_pipe_sddl(std::wstring& sddl) {
    // Базовые права: SYSTEM и встроенные администраторы — полный доступ.
    const std::wstring base = L"D:(A;;GA;;;SY)(A;;GA;;;BA)";
    if (resolve_operator_group()) {
        // GRGW = GENERIC_READ | GENERIC_WRITE, без FILE_CREATE_PIPE_INSTANCE.
        sddl = base + L"(A;;GRGW;;;" + operator_group().sid_string + L")";
        return true;
    }
    sddl = base;
    return false;
}

bool operator_group_available() {
    return resolve_operator_group();
}

void warm_up_group_cache() {
    (void)resolve_operator_group();
}

bool impersonate_client(HANDLE pipe, Identity& identity, std::wstring& error) {
    identity = Identity{};
    if (!ImpersonateNamedPipeClient(pipe)) {
        error = L"ImpersonateNamedPipeClient";
        return false;
    }

    HANDLE token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &token)) {
        error = L"OpenThreadToken";
        revert_to_self();
        return false;
    }

    if (!token_user_sid(token, identity.sid)) {
        error = L"GetTokenInformation(TokenUser)";
        CloseHandle(token);
        revert_to_self();
        return false;
    }

    // Проверка Administrators на impersonated-токене. У неэлевированного
    // администратора filtered token, поэтому результат будет false —
    // он обрабатывается как оператор (SECURITY.md раздел 2).
    identity.is_admin = is_builtin_admin();

    bool operator_member = false;
    if (resolve_operator_group()) {
        check_membership(operator_group().sid.data(), operator_member);
    }
    identity.is_operator = operator_member;
    identity.authenticated = true;

    // Токен клиента нужен планировщику для RunNow от имени пользователя (A-8).
    duplicate_current_thread_token();

    CloseHandle(token);
    revert_to_self();

    context().identity = identity;
    return true;
}

void revert_to_self() {
    RevertToSelf();
}

HANDLE duplicate_client_token() {
    HANDLE thread_token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE,
                         TRUE, &thread_token)) {
        return nullptr;
    }
    HANDLE primary = nullptr;
    BOOL ok = DuplicateTokenEx(thread_token, TOKEN_ALL_ACCESS, nullptr,
                               SecurityImpersonation, TokenPrimary, &primary);
    CloseHandle(thread_token);
    return ok ? primary : nullptr;
}

const Identity* current_identity() {
    return &context().identity;
}

HANDLE current_client_token() {
    return context().primary_token;
}

void set_current_context(const Identity& identity, HANDLE primary_token) {
    context().identity = identity;
    context().primary_token = primary_token;
}

void clear_current_context() {
    context().identity = Identity{};
    context().primary_token = nullptr;
}

} // namespace auth
