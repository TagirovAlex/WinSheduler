// Пользовательские действия установщика WinSheduler.
//
// Реализованы нативной DLL (x64) без внешних зависимостей: .NET на целевой
// машине не требуется, поведение детерминировано и не зависит от локализации
// ОС (используются well-known SID, а не локализованные имена групп).
//
// Действия:
//   ValidatePaths    — проверка INSTALLDIR/DATADIR (immediate);
//   ApplySecurity    — каталоги, ACL, группа, реестр (deferred, SYSTEM);
//   ConfigureService — аргументы, delayed auto start, recovery, preshutdown;
//   MigrateData      — перенос БД со старой раскладки;
//   RemoveData       — удаление данных и группы при KEEPDATA=0;
//   LaunchUi         — запуск UI при STARTUI=1.
//
// Контракты: ./docs/contracts/INSTALL.md, ./docs/contracts/SECURITY.md.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <aclapi.h>
#include <sddl.h>
#include <lm.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winver.h>

#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")

namespace {

constexpr wchar_t kServiceName[] = L"WinSheduler";
constexpr wchar_t kGroupName[] = L"WinSheduler Users";
constexpr wchar_t kRegistryKey[] = L"SOFTWARE\\WinSheduler";
constexpr wchar_t kDataMarker[] = L".winsheduler-data";
constexpr size_t kMaxDirectoryLength = 200;

// Защищённые DACL без наследования: SYSTEM и Administrators — полный доступ,
// встроенная группа Users — чтение и выполнение (только для каталогов программы).
constexpr wchar_t kProgramSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)";
constexpr wchar_t kDataSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

// Права, которых не должно быть у не-администраторов в INSTALLDIR.
constexpr DWORD kWriteBits =
    FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER;
// Для родителя DATADIR достаточно запретить удаление дочерних объектов и смену ACL:
// %ProgramData% штатно разрешает пользователям создавать свои подкаталоги.
constexpr DWORD kDangerousParentBits = FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER;

// Коды ошибок Win32/NETAPI, которые не являются сбоем.
constexpr DWORD kErrAliasExists = 1379;
constexpr DWORD kErrGroupExists = 2223;
constexpr DWORD kErrMemberInAlias = 1378;
constexpr DWORD kErrNoSuchAlias = 1374;
constexpr DWORD kErrGroupNotFound = 2220;

// ---------------------------------------------------------------------------
// Строки и свойства MSI
// ---------------------------------------------------------------------------

std::wstring GetProperty(MSIHANDLE hInstall, const wchar_t* name) {
    wchar_t dummy[1] = { L'\0' };
    DWORD size = 0;
    UINT rc = MsiGetPropertyW(hInstall, name, dummy, &size);
    if (rc == ERROR_SUCCESS || size == 0) {
        return std::wstring();
    }

    std::wstring value(size + 1, L'\0');
    DWORD capacity = size + 1;
    rc = MsiGetPropertyW(hInstall, name, &value[0], &capacity);
    if (rc != ERROR_SUCCESS) {
        return std::wstring();
    }

    value.resize(capacity);
    return value;
}

void LogInfo(MSIHANDLE hInstall, const std::wstring& message) {
    PMSIHANDLE record = MsiCreateRecord(1);
    if (!record) {
        return;
    }
    MsiRecordSetStringW(record, 0, message.c_str());
    MsiProcessMessage(hInstall, INSTALLMESSAGE_INFO, record);
}

UINT Fail(MSIHANDLE hInstall, const std::wstring& message) {
    PMSIHANDLE record = MsiCreateRecord(1);
    if (record) {
        MsiRecordSetStringW(record, 0, message.c_str());
        MsiProcessMessage(hInstall, INSTALLMESSAGE_ERROR, record);
    }
    return ERROR_INSTALL_FAILURE;
}

// Разбор CustomActionData вида "ключ=значение;ключ=значение".
std::wstring DataValue(const std::wstring& data, const wchar_t* key) {
    const std::wstring prefix = std::wstring(key) + L"=";
    size_t start = 0;

    while (start <= data.size()) {
        size_t end = data.find(L';', start);
        std::wstring item = data.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (item.compare(0, prefix.size(), prefix) == 0) {
            return item.substr(prefix.size());
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }

    return std::wstring();
}

std::wstring ToSlashes(const std::wstring& value) {
    std::wstring result = value;
    for (wchar_t& c : result) {
        if (c == L'\\') {
            c = L'/';
        }
    }
    return result;
}

std::wstring ToNative(const std::wstring& value) {
    std::wstring result = value;
    for (wchar_t& c : result) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    return result;
}

std::wstring TrimTrailingSlashes(const std::wstring& value) {
    std::wstring result = value;
    while (result.size() > 3 && (result.back() == L'/' || result.back() == L'\\')) {
        result.pop_back();
    }
    return result;
}

std::wstring Lower(const std::wstring& value) {
    std::wstring result = value;
    for (wchar_t& c : result) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return result;
}

bool IsSameOrUnder(const std::wstring& path, const std::wstring& base) {
    if (path.empty() || base.empty()) {
        return false;
    }

    const std::wstring p = Lower(TrimTrailingSlashes(ToSlashes(path)));
    const std::wstring b = Lower(TrimTrailingSlashes(ToSlashes(base)));
    if (p == b) {
        return true;
    }
    return p.size() > b.size() && p.compare(0, b.size(), b) == 0 && p[b.size()] == L'/';
}

std::wstring ParentPath(const std::wstring& path) {
    const std::wstring trimmed = TrimTrailingSlashes(ToSlashes(path));
    const size_t pos = trimmed.find_last_of(L'/');
    if (pos == std::wstring::npos || pos < 3) {
        return std::wstring();
    }
    return trimmed.substr(0, pos);
}

bool PathExists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    return GetFileAttributesW(ToNative(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------------------
// Каталоги и ACL
// ---------------------------------------------------------------------------

bool EnsureDirectory(const std::wstring& path) {
    const std::wstring native = ToNative(TrimTrailingSlashes(path));
    if (native.size() < 3) {
        return false;
    }

    std::wstring current = native.substr(0, 2);
    size_t index = 2;

    while (index < native.size()) {
        while (index < native.size() && native[index] == L'\\') {
            ++index;
        }
        const size_t end = native.find(L'\\', index);
        const std::wstring segment = native.substr(
            index, end == std::wstring::npos ? std::wstring::npos : end - index);
        if (!segment.empty()) {
            current += L"\\";
            current += segment;
            if (!CreateDirectoryW(current.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
        if (end == std::wstring::npos) {
            break;
        }
        index = end + 1;
    }

    return true;
}

DWORD NormalizeAccessMask(DWORD mask) {
    if ((mask & GENERIC_ALL) != 0) {
        return FILE_ALL_ACCESS | WRITE_DAC | WRITE_OWNER;
    }
    if ((mask & GENERIC_WRITE) != 0) {
        mask |= FILE_GENERIC_WRITE | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD;
    }
    if ((mask & GENERIC_READ) != 0) {
        mask |= FILE_GENERIC_READ;
    }
    if ((mask & GENERIC_EXECUTE) != 0) {
        mask |= FILE_GENERIC_EXECUTE;
    }
    return mask;
}

DWORD AclMaskForSid(PACL acl, PSID sid) {
    if (!acl || !sid) {
        return 0;
    }

    DWORD mask = 0;
    for (WORD i = 0; i < acl->AceCount; ++i) {
        LPVOID ace = nullptr;
        if (!GetAce(acl, i, &ace) || !ace) {
            continue;
        }

        const ACE_HEADER* header = static_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            continue;
        }

        const ACCESS_ALLOWED_ACE* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
        PSID aceSid = reinterpret_cast<PSID>(const_cast<DWORD*>(&allowed->SidStart));
        if (EqualSid(aceSid, sid)) {
            mask |= allowed->Mask;
        }
    }

    return NormalizeAccessMask(mask);
}

// true — каталог доступен не-администраторам на указанные права.
bool DirectoryWritableByNonAdmins(const std::wstring& path, DWORD bits, std::wstring& detail) {
    const std::wstring native = ToNative(path);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;

    DWORD rc = GetNamedSecurityInfoW(const_cast<wchar_t*>(native.c_str()), SE_FILE_OBJECT,
                                     DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                                     &descriptor);
    if (rc != ERROR_SUCCESS) {
        detail = L"не удалось прочитать ACL каталога " + path;
        return true;
    }

    bool unsafe = false;
    const wchar_t* sids[] = { L"S-1-5-32-545", L"S-1-5-11" };

    for (const wchar_t* sidText : sids) {
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(sidText, &sid)) {
            continue;
        }
        const DWORD mask = AclMaskForSid(dacl, sid);
        LocalFree(sid);
        if ((mask & bits) != 0) {
            unsafe = true;
            break;
        }
    }

    if (descriptor) {
        LocalFree(descriptor);
    }
    if (unsafe) {
        detail = L"каталог " + path + L" доступен не-администраторам на запись или смену ACL";
    }
    return unsafe;
}

bool SetDirectoryAcl(const std::wstring& path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor,
                                                             nullptr)) {
        return false;
    }

    PACL dacl = nullptr;
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present) {
        LocalFree(descriptor);
        return false;
    }

    const std::wstring native = ToNative(path);
    const DWORD rc = SetNamedSecurityInfoW(
        const_cast<wchar_t*>(native.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl,
        nullptr);
    LocalFree(descriptor);

    return rc == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Проверка путей
// ---------------------------------------------------------------------------

struct PathPolicy {
    DWORD selfBits;
    DWORD parentBits;
};

bool ValidatePathSyntax(const std::wstring& raw, const wchar_t* label, std::wstring& error) {
    if (raw.empty()) {
        error = std::wstring(label) + L": путь не задан.";
        return false;
    }

    std::wstring path = TrimTrailingSlashes(ToSlashes(raw));

    if (path.size() < 3) {
        error = std::wstring(label) + L": путь слишком короткий.";
        return false;
    }
    if (path[0] == L'/' || path[1] != L':') {
        error = std::wstring(label) + L": UNC, сетевые и относительные пути запрещены.";
        return false;
    }
    if (!std::iswalpha(static_cast<wint_t>(path[0]))) {
        error = std::wstring(label) + L": недопустимая буква диска.";
        return false;
    }

    for (size_t i = 0; i < path.size(); ++i) {
        const wchar_t c = path[i];
        if (i == 1 || c == L'/') {
            continue;
        }
        if (c < 0x20 || std::wcschr(L"<>:\"|?*", c) != nullptr) {
            error = std::wstring(label) + L": недопустимый символ в пути.";
            return false;
        }
    }

    size_t start = 3;
    while (start <= path.size()) {
        const size_t end = path.find(L'/', start);
        const std::wstring segment = path.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (segment.empty()) {
            error = std::wstring(label) + L": пустой сегмент пути.";
            return false;
        }
        if (segment == L"." || segment == L"..") {
            error = std::wstring(label) + L": переходы .. и . запрещены.";
            return false;
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }

    // Нормализация: путь должен совпадать с каноническим (ловит .. и лишние слэши).
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length =
        GetFullPathNameW(ToNative(path).c_str(), static_cast<DWORD>(buffer.size()), buffer.data(),
                         nullptr);
    if (length == 0 || length >= buffer.size()) {
        error = std::wstring(label) + L": не удалось нормализовать путь.";
        return false;
    }
    const std::wstring normalized = Lower(TrimTrailingSlashes(ToSlashes(buffer.data())));
    if (normalized != Lower(path)) {
        error = std::wstring(label) + L": путь содержит .. или лишние разделители.";
        return false;
    }

    const wchar_t root[4] = { path[0], L':', L'/', L'\0' };
    if (GetDriveTypeW(root) != DRIVE_FIXED) {
        error = std::wstring(label) + L": требуется локальный фиксированный диск.";
        return false;
    }

    if (path.size() == 3) {
        error = std::wstring(label) + L": корень диска использовать запрещено.";
        return false;
    }

    if (path.size() > kMaxDirectoryLength) {
        error = std::wstring(label) + L": длина пути больше 200 символов.";
        return false;
    }

    wchar_t profile[MAX_PATH * 2] = { L'\0' };
    wchar_t temp[MAX_PATH * 2] = { L'\0' };
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH * 2) > 0) {
        if (IsSameOrUnder(path, profile)) {
            error = std::wstring(label) + L": профиль пользователя использовать запрещено.";
            return false;
        }
        if (IsSameOrUnder(path, std::wstring(profile) + L"/Downloads")) {
            error = std::wstring(label) + L": каталог загрузок использовать запрещено.";
            return false;
        }
    }
    if (GetEnvironmentVariableW(L"TEMP", temp, MAX_PATH * 2) > 0 && IsSameOrUnder(path, temp)) {
        error = std::wstring(label) + L": каталог %TEMP% использовать запрещено.";
        return false;
    }

    return true;
}

bool ValidatePathAcl(const std::wstring& path, const PathPolicy& policy, std::wstring& error) {
    const std::wstring target = TrimTrailingSlashes(ToSlashes(path));
    std::wstring detail;

    if (PathExists(target)) {
        if (DirectoryWritableByNonAdmins(target, policy.selfBits, detail)) {
            error = L"INSTALLDIR/DATADIR: " + detail;
            return false;
        }
        const std::wstring parent = ParentPath(target);
        if (!parent.empty() && DirectoryWritableByNonAdmins(parent, policy.parentBits, detail)) {
            error = L"родительский каталог небезопасен: " + detail;
            return false;
        }
        return true;
    }

    // Каталога ещё нет: проверяем ближайший существующий родитель.
    std::wstring ancestor = ParentPath(target);
    while (!ancestor.empty() && !PathExists(ancestor)) {
        ancestor = ParentPath(ancestor);
    }
    if (ancestor.empty()) {
        error = L"не удалось найти существующий родительский каталог.";
        return false;
    }
    if (DirectoryWritableByNonAdmins(ancestor, policy.parentBits, detail)) {
        error = L"родительский каталог небезопасен: " + detail;
        return false;
    }

    return true;
}

struct OsVersionInfo {
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    const wchar_t* source = L"unknown";
};

bool ReadRegistryVersion(OsVersionInfo& info) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0,
                      KEY_READ | KEY_WOW64_64KEY,
                      &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD major = 0;
    DWORD minor = 0;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    RegQueryValueExW(key, L"CurrentMajorVersionNumber", nullptr, &type,
                     reinterpret_cast<LPBYTE>(&major), &size);

    size = sizeof(DWORD);
    type = 0;
    RegQueryValueExW(key, L"CurrentMinorVersionNumber", nullptr, &type,
                     reinterpret_cast<LPBYTE>(&minor), &size);

    wchar_t build[32] = {};
    size = sizeof(build);
    type = 0;
    const LONG buildResult = RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type,
                                              reinterpret_cast<LPBYTE>(build), &size);

    if (major == 0) {
        wchar_t version[32] = {};
        size = sizeof(version);
        type = 0;
        if (RegQueryValueExW(key, L"CurrentVersion", nullptr, &type,
                             reinterpret_cast<LPBYTE>(version), &size) == ERROR_SUCCESS &&
            type == REG_SZ) {
            unsigned long versionMajor = 0;
            unsigned long versionMinor = 0;
            if (swscanf_s(version, L"%lu.%lu", &versionMajor, &versionMinor) >= 1) {
                major = static_cast<DWORD>(versionMajor);
                minor = static_cast<DWORD>(versionMinor);
            }
        }
    }

    RegCloseKey(key);

    if (major == 0 || buildResult != ERROR_SUCCESS || type != REG_SZ || build[0] == L'\0') {
        return false;
    }

    info.major = major;
    info.minor = minor;
    info.build = static_cast<DWORD>(std::wcstoul(build, nullptr, 10));
    info.source = L"registry";
    return true;
}

bool ReadKernelVersion(OsVersionInfo& info) {
    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0) {
        return false;
    }

    const std::wstring path = std::wstring(systemDir) + L"\\kernel32.dll";
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (size == 0) {
        return false;
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return false;
    }

    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed), &length) ||
        fixed == nullptr) {
        return false;
    }

    info.major = HIWORD(fixed->dwFileVersionMS);
    info.minor = LOWORD(fixed->dwFileVersionMS);
    info.build = HIWORD(fixed->dwFileVersionLS);
    info.source = L"kernel32";
    return info.major != 0;
}

bool ReadRtlVersion(OsVersionInfo& info) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) {
        return false;
    }

    OSVERSIONINFOW raw{};
    raw.dwOSVersionInfoSize = sizeof(raw);
    if (rtlGetVersion(&raw) != 0) {
        return false;
    }

    info.major = raw.dwMajorVersion;
    info.minor = raw.dwMinorVersion;
    info.build = raw.dwBuildNumber;
    info.source = L"RtlGetVersion";
    return true;
}

void LogOsVersion(MSIHANDLE hInstall, const OsVersionInfo& info) {
    LogInfo(hInstall,
            L"WinSheduler: OS check source=" + std::wstring(info.source) +
            L" version=" + std::to_wstring(info.major) + L"." +
            std::to_wstring(info.minor) +
            L" build=" + std::to_wstring(info.build));
}

bool CheckWindowsVersion(MSIHANDLE hInstall) {
    if (GetProperty(hInstall, L"ALLOWUNSUPPORTEDOS") == L"1") {
        LogInfo(hInstall, L"WinSheduler: OS check пропущен (ALLOWUNSUPPORTEDOS=1).");
        return true;
    }

    // Основной источник — реестр: он не подвержен compatibility shim,
    // из-за которого RtlGetVersion внутри msiexec может вернуть 6.0.6000.
    OsVersionInfo registry;
    if (ReadRegistryVersion(registry)) {
        LogOsVersion(hInstall, registry);
        return registry.major > 10 ||
               (registry.major == 10 && registry.build >= 14393);
    }

    // Резерв — версия kernel32.dll, тоже не шимится.
    OsVersionInfo kernel;
    if (ReadKernelVersion(kernel)) {
        LogOsVersion(hInstall, kernel);
        return kernel.major > 10 ||
               (kernel.major == 10 && kernel.build >= 14393);
    }

    // Последний резерв — RtlGetVersion; при сомнении установку не блокируем.
    OsVersionInfo rtl;
    if (ReadRtlVersion(rtl)) {
        LogOsVersion(hInstall, rtl);
        if (rtl.major > 10 || (rtl.major == 10 && rtl.build >= 14393)) {
            return true;
        }
    }

    LogInfo(hInstall,
            L"WinSheduler: OS check — версию определить не удалось, установка продолжается.");
    return true;
}

// ---------------------------------------------------------------------------
// Группа, служба, реестр
// ---------------------------------------------------------------------------

bool EnsureLocalGroup(const std::wstring& name) {
    LOCALGROUP_INFO_1 info{};
    info.lgrpi1_name = const_cast<LPWSTR>(name.c_str());
    info.lgrpi1_comment = const_cast<LPWSTR>(L"Пользователи WinSheduler");

    const DWORD rc = NetLocalGroupAdd(nullptr, 1, reinterpret_cast<LPBYTE>(&info), nullptr);
    return rc == 0 || rc == kErrAliasExists || rc == kErrGroupExists;
}

bool AddLocalGroupMember(const std::wstring& group, const std::wstring& member) {
    LOCALGROUP_MEMBERS_INFO_3 info{};
    info.lgrmi3_domainandname = const_cast<LPWSTR>(member.c_str());

    const DWORD rc =
        NetLocalGroupAddMembers(nullptr, group.c_str(), 3, reinterpret_cast<LPBYTE>(&info), 1);
    return rc == 0 || rc == kErrMemberInAlias;
}

void RemoveLocalGroup(const std::wstring& name) {
    const DWORD rc = NetLocalGroupDel(nullptr, name.c_str());
    if (rc != 0 && rc != kErrNoSuchAlias && rc != kErrGroupNotFound) {
        // Не валим удаление: группа могла быть удалена администратором.
    }
}

bool LookupAccountSidText(const std::wstring& name, std::wstring& sidText) {
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD sidSize = sizeof(sidBuffer);
    wchar_t domain[256] = { L'\0' };
    DWORD domainSize = 256;
    SID_NAME_USE use = SidTypeUnknown;

    if (!LookupAccountNameW(nullptr, name.c_str(), sidBuffer, &sidSize, domain, &domainSize, &use)) {
        return false;
    }

    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<PSID>(sidBuffer), &text)) {
        return false;
    }
    sidText = text;
    LocalFree(text);
    return true;
}

bool SetRegistryString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                       const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const DWORD rc = RegSetValueExW(key, name, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool StopService(const std::wstring& name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return true; // нет доступа к SCM — службы точно нет
    }

    SC_HANDLE service = OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scm);
        return true; // служба уже удалена
    }

    SERVICE_STATUS status{};
    if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        for (int i = 0; i < 60; ++i) {
            if (!QueryServiceStatus(service, &status)) {
                break;
            }
            if (status.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
            Sleep(500);
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool DeleteDirectoryTree(const std::wstring& path) {
    std::wstring from = ToNative(TrimTrailingSlashes(path));
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    return SHFileOperationW(&operation) == 0;
}

bool CopyDatabase(const std::wstring& source, const std::wstring& target, MSIHANDLE hInstall) {
    if (PathExists(target)) {
        const std::wstring backup = target + L".preinstall.bak";
        if (!PathExists(backup)) {
            if (!CopyFileW(ToNative(target).c_str(), ToNative(backup).c_str(), TRUE)) {
                return false;
            }
        }
        LogInfo(hInstall, L"WinSheduler: целевая БД уже существует, перенос пропущен (" + target +
                              L").");
        return true;
    }

    if (!EnsureDirectory(ParentPath(target))) {
        return false;
    }
    if (!CopyFileW(ToNative(source).c_str(), ToNative(target).c_str(), TRUE)) {
        return false;
    }
    LogInfo(hInstall, L"WinSheduler: БД перенесена из " + source + L" в " + target + L".");
    return true;
}

bool WriteMarker(const std::wstring& datadir, const std::wstring& version) {
    const std::wstring path = datadir + L"/" + kDataMarker;
    HANDLE file = CreateFileW(ToNative(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::string text = "WinSheduler data directory. Marker used by uninstall.\r\nVersion: ";
    if (!version.empty()) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, version.c_str(), -1, nullptr, 0, nullptr,
                                             nullptr);
        if (size > 1) {
            std::string utf8(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, version.c_str(), -1, &utf8[0], size, nullptr, nullptr);
            utf8.resize(static_cast<size_t>(size - 1));
            text += utf8;
        }
    }
    text += "\r\n";

    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

}  // namespace

// ---------------------------------------------------------------------------
// Экспортируемые действия
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) UINT __stdcall ValidatePaths(MSIHANDLE hInstall) {
    try {
        const std::wstring installdir = GetProperty(hInstall, L"INSTALLDIR");
        const std::wstring datadir = GetProperty(hInstall, L"DATADIR");
        const std::wstring allowUnsafe = GetProperty(hInstall, L"ALLOWUNSAFEPATH");

        LogInfo(hInstall, L"WinSheduler: INSTALLDIR=" + installdir + L", DATADIR=" + datadir);

        if (!CheckWindowsVersion(hInstall)) {
            return Fail(hInstall,
                        L"WinSheduler требует Windows 10 1607 (build 14393) или Windows Server 2016 "
                        L"и новее.");
        }

        std::wstring error;
        if (!ValidatePathSyntax(installdir, L"INSTALLDIR", error)) {
            return Fail(hInstall, L"WinSheduler: " + error);
        }
        if (!ValidatePathSyntax(datadir, L"DATADIR", error)) {
            return Fail(hInstall, L"WinSheduler: " + error);
        }

        if (allowUnsafe == L"1") {
            LogInfo(hInstall,
                    L"WinSheduler: проверка ACL отключена (ALLOWUNSAFEPATH=1). "
                    L"Режим только для лаборатории.");
            return ERROR_SUCCESS;
        }

        const PathPolicy programPolicy{ kWriteBits, kWriteBits };
        const PathPolicy dataPolicy{ kWriteBits, kDangerousParentBits };

        if (!ValidatePathAcl(installdir, programPolicy, error)) {
            return Fail(hInstall,
                        L"WinSheduler: небезопасный INSTALLDIR — " + error +
                            L". Укажите другой каталог или ALLOWUNSAFEPATH=1 (только лаборатория).");
        }
        if (!ValidatePathAcl(datadir, dataPolicy, error)) {
            return Fail(hInstall,
                        L"WinSheduler: небезопасный DATADIR — " + error +
                            L". Укажите другой каталог или ALLOWUNSAFEPATH=1 (только лаборатория).");
        }

        if (IsSameOrUnder(datadir, installdir) || IsSameOrUnder(installdir, datadir)) {
            LogInfo(hInstall, L"WinSheduler: предупреждение — INSTALLDIR и DATADIR вложены.");
        }

        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall ApplySecurity(MSIHANDLE hInstall) {
    try {
        const std::wstring data = GetProperty(hInstall, L"CustomActionData");
        const std::wstring installdir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"installdir")));
        const std::wstring datadir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"datadir")));
        const std::wstring createGroup = DataValue(data, L"creategroup");
        const std::wstring addInstaller = DataValue(data, L"addinstaller");
        const std::wstring domain = DataValue(data, L"userdomain");
        const std::wstring user = DataValue(data, L"username");
        const std::wstring version = DataValue(data, L"version");

        if (installdir.empty() || datadir.empty()) {
            return Fail(hInstall, L"WinSheduler: не заданы INSTALLDIR или DATADIR.");
        }

        const std::wstring serviceDir = installdir + L"/service";
        const std::wstring uiDir = installdir + L"/ui";
        const std::wstring dataDir = datadir + L"/data";
        const std::wstring logDir = datadir + L"/logs";
        const std::wstring outputDir = logDir + L"/output";

        for (const std::wstring& directory :
             { installdir, serviceDir, uiDir, datadir, dataDir, logDir, outputDir }) {
            if (!EnsureDirectory(directory)) {
                return Fail(hInstall, L"WinSheduler: не удалось создать каталог " + directory);
            }
        }

        for (const std::wstring& directory : { installdir, serviceDir, uiDir }) {
            if (!SetDirectoryAcl(directory, kProgramSddl)) {
                return Fail(hInstall, L"WinSheduler: не удалось задать ACL каталога " + directory);
            }
        }
        for (const std::wstring& directory : { datadir, dataDir, logDir, outputDir }) {
            if (!SetDirectoryAcl(directory, kDataSddl)) {
                return Fail(hInstall, L"WinSheduler: не удалось задать ACL каталога " + directory);
            }
        }

        if (!WriteMarker(datadir, version)) {
            LogInfo(hInstall, L"WinSheduler: предупреждение — не удалось создать маркер данных.");
        }

        std::wstring groupSid;
        if (createGroup == L"1") {
            if (!EnsureLocalGroup(kGroupName)) {
                return Fail(hInstall, L"WinSheduler: не удалось создать группу " +
                                          std::wstring(kGroupName) + L".");
            }

            if (addInstaller == L"1" && !user.empty() && Lower(user) != L"system" &&
                user.back() != L'$') {
                const std::wstring member = domain.empty() ? user : domain + L"\\" + user;
                if (!AddLocalGroupMember(kGroupName, member)) {
                    LogInfo(hInstall, L"WinSheduler: предупреждение — не удалось добавить " +
                                          member + L" в группу.");
                }
            }
        }

        if (!LookupAccountSidText(kGroupName, groupSid)) {
            groupSid.clear();
            LogInfo(hInstall, L"WinSheduler: предупреждение — SID группы не определён.");
        }

        if (!SetRegistryString(HKEY_LOCAL_MACHINE, kRegistryKey, L"InstallDir", ToNative(installdir)) ||
            !SetRegistryString(HKEY_LOCAL_MACHINE, kRegistryKey, L"DataDir", ToNative(datadir)) ||
            !SetRegistryString(HKEY_LOCAL_MACHINE, kRegistryKey, L"GroupSid", groupSid) ||
            !SetRegistryString(HKEY_LOCAL_MACHINE, kRegistryKey, L"Version", version)) {
            return Fail(hInstall, L"WinSheduler: не удалось записать параметры в реестр.");
        }

        LogInfo(hInstall, L"WinSheduler: каталоги, ACL и группа настроены.");
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall ConfigureService(MSIHANDLE hInstall) {
    try {
        const std::wstring data = GetProperty(hInstall, L"CustomActionData");
        const std::wstring serviceExe =
            TrimTrailingSlashes(ToSlashes(DataValue(data, L"serviceexe")));
        const std::wstring datadir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"datadir")));

        if (serviceExe.empty() || datadir.empty()) {
            return Fail(hInstall, L"WinSheduler: не заданы путь службы или DATADIR.");
        }

        const std::wstring binaryPath = L"\"" + ToNative(serviceExe) + L"\" --dbpath \"" +
                                        ToNative(datadir) + L"\\data\\scheduler.db\" --logdir \"" +
                                        ToNative(datadir) + L"\\logs\"";

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) {
            return Fail(hInstall, L"WinSheduler: не удалось открыть менеджер служб.");
        }

        SC_HANDLE service = OpenServiceW(
            scm, kServiceName, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START);
        if (!service) {
            CloseServiceHandle(scm);
            return Fail(hInstall, L"WinSheduler: служба не зарегистрирована.");
        }

        BOOL ok = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_AUTO_START,
                                       SERVICE_NO_CHANGE, binaryPath.c_str(), nullptr, nullptr,
                                       nullptr, nullptr, nullptr, nullptr);
        if (!ok) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return Fail(hInstall, L"WinSheduler: не удалось задать аргументы службы.");
        }

        SERVICE_DELAYED_AUTO_START_INFO delayed{ TRUE };
        ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed);

        SC_ACTION actions[3] = {
            { SC_ACTION_RESTART, 5000 },
            { SC_ACTION_RESTART, 30000 },
            { SC_ACTION_RESTART, 60000 },
        };
        SERVICE_FAILURE_ACTIONSW failure{};
        failure.dwResetPeriod = 86400;
        failure.cActions = 3;
        failure.lpsaActions = actions;
        ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

        SERVICE_PRESHUTDOWN_INFO preshutdown{};
        preshutdown.dwPreshutdownTimeout = 60000;
        ChangeServiceConfig2W(service, SERVICE_CONFIG_PRESHUTDOWN_INFO, &preshutdown);

        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        LogInfo(hInstall, L"WinSheduler: служба настроена (" + binaryPath + L").");
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall MigrateData(MSIHANDLE hInstall) {
    try {
        const std::wstring data = GetProperty(hInstall, L"CustomActionData");
        const std::wstring oldDir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"olddatadir")));
        const std::wstring datadir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"datadir")));

        if (datadir.empty()) {
            return Fail(hInstall, L"WinSheduler: не задан DATADIR для миграции.");
        }

        std::vector<std::wstring> candidates;
        if (!oldDir.empty()) {
            candidates.push_back(oldDir);
        } else {
            wchar_t programData[MAX_PATH * 2] = { L'\0' };
            if (GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH * 2) > 0) {
                candidates.push_back(std::wstring(programData) + L"/WinSheduler/service/data");
                candidates.push_back(std::wstring(programData) + L"/WinSheduler/legacy/data");
            }
        }

        const std::wstring target = datadir + L"/data/scheduler.db";
        for (const std::wstring& candidate : candidates) {
            const std::wstring source = candidate + L"/scheduler.db";
            if (!PathExists(source)) {
                continue;
            }
            if (!CopyDatabase(source, target, hInstall)) {
                return Fail(hInstall, L"WinSheduler: не удалось перенести БД из " + source + L".");
            }
            return ERROR_SUCCESS;
        }

        LogInfo(hInstall, L"WinSheduler: старая БД не найдена, миграция не требуется.");
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall RemoveData(MSIHANDLE hInstall) {
    try {
        const std::wstring data = GetProperty(hInstall, L"CustomActionData");
        const std::wstring datadir = TrimTrailingSlashes(ToSlashes(DataValue(data, L"datadir")));
        const std::wstring keepData = DataValue(data, L"keepdata");

        StopService(kServiceName);

        if (keepData != L"1") {
            const std::wstring marker = datadir + L"/" + kDataMarker;
            if (!datadir.empty() && PathExists(marker)) {
                if (!DeleteDirectoryTree(datadir)) {
                    return Fail(hInstall, L"WinSheduler: не удалось удалить каталог данных " +
                                              datadir + L".");
                }
                LogInfo(hInstall, L"WinSheduler: каталог данных удалён (" + datadir + L").");
            } else {
                LogInfo(hInstall,
                        L"WinSheduler: маркер данных не найден, каталог не удаляется (KEEPDATA=0).");
            }
            RemoveLocalGroup(kGroupName);
        } else {
            LogInfo(hInstall, L"WinSheduler: данные сохранены (KEEPDATA=1).");
        }

        RegDeleteTreeW(HKEY_LOCAL_MACHINE, kRegistryKey);
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall NormalizePaths(MSIHANDLE hInstall) {
    try {
        // Windows Installer не принимает прямые слэши в свойствах каталогов
        // и считает такой путь сетевым. Нормализуем значения из реестра
        // и командной строки до CostFinalize.
        for (const wchar_t* name : { L"INSTALLDIR", L"DATADIR", L"OLDDATADIR" }) {
            const std::wstring value = GetProperty(hInstall, name);
            if (value.find(L'/') == std::wstring::npos) {
                continue;
            }

            const std::wstring normalized = ToNative(value);
            MsiSetPropertyW(hInstall, name, normalized.c_str());
            LogInfo(hInstall,
                    L"WinSheduler: " + std::wstring(name) + L" нормализован: " + normalized);
        }

        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

bool FixRegistryPathValue(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size == 0) {
        return true;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS ||
        type != REG_SZ) {
        return true;
    }

    const std::wstring value(buffer.data());
    if (value.find(L'/') == std::wstring::npos) {
        return true;
    }

    const std::wstring fixed = ToNative(value);
    const DWORD bytes = static_cast<DWORD>((fixed.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(fixed.c_str()), bytes) == ERROR_SUCCESS;
}

extern "C" __declspec(dllexport) UINT __stdcall FixRegistryPaths(MSIHANDLE hInstall) {
    try {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          kRegistryKey,
                          0,
                          KEY_READ | KEY_WRITE | KEY_WOW64_64KEY,
                          &key) != ERROR_SUCCESS) {
            return ERROR_SUCCESS;
        }

        const bool ok = FixRegistryPathValue(key, L"InstallDir") &&
                        FixRegistryPathValue(key, L"DataDir");
        RegCloseKey(key);

        if (!ok) {
            return Fail(hInstall, L"WinSheduler: не удалось исправить пути в реестре.");
        }

        LogInfo(hInstall, L"WinSheduler: пути в реестре нормализованы.");
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall CloseUi(MSIHANDLE hInstall) {
    try {
        // UI может блокировать WinSheduler.UI.exe при обновлении.
        // Закрываем его до InstallInitialize/замены файлов.
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return ERROR_SUCCESS;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (wcscmp(entry.szExeFile, L"WinSheduler.UI.exe") == 0) {
                    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                    if (process) {
                        TerminateProcess(process, 0);
                        CloseHandle(process);
                        LogInfo(hInstall, L"WinSheduler: UI закрыт перед обновлением.");
                    }
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_SUCCESS;
    }
}

extern "C" __declspec(dllexport) UINT __stdcall LaunchUi(MSIHANDLE hInstall) {
    try {
        const std::wstring data = GetProperty(hInstall, L"CustomActionData");
        const std::wstring uiPath = ToNative(DataValue(data, L"uipath"));
        if (uiPath.empty() || !PathExists(uiPath)) {
            LogInfo(hInstall, L"WinSheduler: UI не найден, запуск пропущен.");
            return ERROR_SUCCESS;
        }

        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", uiPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            LogInfo(hInstall, L"WinSheduler: не удалось запустить UI после установки.");
        }
        return ERROR_SUCCESS;
    } catch (...) {
        return ERROR_INSTALL_FAILURE;
    }
}
