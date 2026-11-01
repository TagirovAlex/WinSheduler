#include "db_path.h"

#include <windows.h>
#include <aclapi.h>

#include <algorithm>
#include <filesystem>

namespace {

std::wstring normalize_separators(const std::wstring& path) {
    std::wstring result = path;
    std::replace(result.begin(), result.end(), L'\\', L'/');
    return result;
}

bool is_ascii_alpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// Права, которые дают не-администратору менять содержимое каталога.
constexpr DWORD kWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
    DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;

bool ace_grants_write_to_well_known(ACCESS_ALLOWED_ACE* ace) {
    if (!ace) return false;
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
    if ((ace->Mask & kWriteMask) == 0) return false;

    const PSID sid = reinterpret_cast<PSID>(&ace->SidStart);
    const WELL_KNOWN_SID_TYPE types[] = {
        WinWorldSid,
        WinBuiltinUsersSid,
        WinAuthenticatedUserSid,
        WinInteractiveSid,
    };

    for (WELL_KNOWN_SID_TYPE type : types) {
        BYTE buffer[SECURITY_MAX_SID_SIZE] = {};
        DWORD size = sizeof(buffer);
        if (!CreateWellKnownSid(type, nullptr, buffer, &size)) continue;
        if (EqualSid(sid, buffer)) return true;
    }
    return false;
}

} // namespace

bool db_path_is_valid(const std::wstring& path, std::wstring& error) {
    error.clear();
    if (path.empty()) {
        error = L"Путь к базе данных не задан";
        return false;
    }
    if (path.size() < 4 || !is_ascii_alpha(path[0]) || path[1] != L':' ||
        (path[2] != L'/' && path[2] != L'\\')) {
        error = L"Путь к базе данных должен быть локальным абсолютным (например, d:/data/scheduler.db)";
        return false;
    }

    const std::wstring normalized = normalize_separators(path);
    if (normalized.rfind(L"//", 0) == 0) {
        error = L"UNC- и device-пути запрещены";
        return false;
    }
    if (normalized.find(L"//") != std::wstring::npos) {
        error = L"Путь содержит пустой сегмент";
        return false;
    }
    if (normalized.find(L'/') == std::wstring::npos) {
        error = L"Путь должен указывать на файл";
        return false;
    }
    if (normalized.back() == L'/') {
        error = L"Путь должен указывать на файл, а не на каталог";
        return false;
    }

    // Альтернативные потоки: двоеточие допустимо только после буквы диска.
    if (normalized.find(L':', 2) != std::wstring::npos) {
        error = L"Альтернативные потоки в пути запрещены";
        return false;
    }

    size_t start = 0;
    while (start <= normalized.size()) {
        const size_t end = normalized.find(L'/', start);
        const std::wstring segment =
            normalized.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (start > 2 && segment.empty()) {
            error = L"Путь содержит пустой сегмент";
            return false;
        }
        if (segment == L".." || segment == L".") {
            error = L"Сегменты '..' и '.' в пути запрещены";
            return false;
        }
        for (wchar_t c : segment) {
            if (c < 0x20) {
                error = L"Путь содержит управляющие символы";
                return false;
            }
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return true;
}

bool db_dir_acl_is_restricted(const std::wstring& dir, std::wstring& error) {
    error.clear();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    const DWORD status = GetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) {
        error = L"Не удалось прочитать ACL каталога (код " + std::to_wstring(status) + L")";
        return false;
    }

    bool restricted = true;
    if (!dacl) {
        // Отсутствие DACL означает полный доступ для всех.
        restricted = false;
        error = L"У каталога нет DACL: доступ не ограничен";
    } else {
        ACL_SIZE_INFORMATION info{};
        if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation)) {
            LocalFree(descriptor);
            error = L"Не удалось разобрать ACL каталога";
            return false;
        }
        for (DWORD i = 0; i < info.AceCount; ++i) {
            LPVOID ace = nullptr;
            if (!GetAce(dacl, i, &ace)) continue;
            if (ace_grants_write_to_well_known(static_cast<ACCESS_ALLOWED_ACE*>(ace))) {
                restricted = false;
                error = L"Каталог доступен на запись обычным пользователям";
                break;
            }
        }
    }

    LocalFree(descriptor);
    return restricted;
}

bool db_ensure_directory(const std::wstring& dir, std::wstring& error) {
    error.clear();
    if (dir.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
    if (ec) {
        const std::string message = ec.message();
        error = L"Не удалось создать каталог: " + std::wstring(message.begin(), message.end());
        return false;
    }
    return true;
}
