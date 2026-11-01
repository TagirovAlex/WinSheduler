#include "auth_secret.h"
#include "config.h"
#include <dpapi.h>
#include <vector>

#pragma comment(lib, "crypt32.lib")

// Флаг DPAPI «только SYSTEM»: некоторые версии Windows SDK не объявляют его,
// хотя значение задокументировано (CryptProtectData, dwFlags).
#ifndef CRYPTPROTECT_SYSTEM
#define CRYPTPROTECT_SYSTEM 0x00000002
#endif

namespace auth {

namespace {

std::string to_base64(const BYTE* data, DWORD size) {
    DWORD chars = 0;
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars)) {
        return {};
    }
    std::string out(chars, '\0');
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &chars)) {
        return {};
    }
    out.resize(chars);
    return out;
}

bool from_base64(const std::string& encoded, std::vector<BYTE>& out) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()),
                              CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) {
        return false;
    }
    out.resize(size);
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()),
                              CRYPT_STRING_BASE64, out.data(), &size, nullptr, nullptr)) {
        return false;
    }
    out.resize(size);
    return true;
}

} // namespace

std::wstring protect_secret(const std::wstring& plain) {
    if (plain.empty()) return {};
    std::string utf8 = ws2u(plain);
    if (utf8.empty()) return {};

    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE*>(utf8.data());
    input.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB output = {};
    if (!CryptProtectData(&input, L"WinSheduler.RunAsPassword", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_SYSTEM | CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        SecureZeroMemory(utf8.data(), utf8.size());
        return {};
    }

    std::string encoded = to_base64(output.pbData, output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    SecureZeroMemory(utf8.data(), utf8.size());
    return u2ws(encoded);
}

bool unprotect_secret(const std::wstring& base64, std::wstring& plain, bool legacy_local_machine) {
    plain.clear();
    if (base64.empty()) return false;

    std::vector<BYTE> blob;
    if (!from_base64(ws2u(base64), blob) || blob.empty()) return false;

    DATA_BLOB input = {};
    input.pbData = blob.data();
    input.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB output = {};
    DWORD flags = CRYPTPROTECT_UI_FORBIDDEN;
    if (legacy_local_machine) {
        flags |= CRYPTPROTECT_LOCAL_MACHINE;
    } else {
        flags |= CRYPTPROTECT_SYSTEM;
    }

    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, flags, &output)) {
        SecureZeroMemory(blob.data(), blob.size());
        return false;
    }

    std::string utf8(reinterpret_cast<char*>(output.pbData), output.cbData);
    plain = u2ws(utf8);
    SecureZeroMemory(utf8.data(), utf8.size());
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    SecureZeroMemory(blob.data(), blob.size());
    return true;
}

void wipe_secret(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

} // namespace auth
