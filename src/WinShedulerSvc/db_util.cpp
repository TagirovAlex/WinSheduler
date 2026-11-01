#include "db_util.h"
#include "config.h"
#include "json.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <cwchar>

namespace {

// Форматирование UTC-времени через std::tm.
std::wstring format_utc(std::time_t value, const wchar_t* pattern) {
    std::tm tm{};
    gmtime_s(&tm, &value);
    wchar_t buf[64] = {};
    std::wcsftime(buf, sizeof(buf) / sizeof(buf[0]), pattern, &tm);
    return std::wstring(buf);
}

} // namespace

std::wstring db_utc_now() {
    return format_utc(std::time(nullptr), L"%Y-%m-%dT%H:%M:%SZ");
}

std::wstring db_utc_days_ago(int days) {
    const std::time_t now = std::time(nullptr);
    const std::time_t cutoff = now - static_cast<std::time_t>(days) * 24 * 60 * 60;
    return format_utc(cutoff, L"%Y-%m-%dT%H:%M:%SZ");
}

std::wstring db_utc_timestamp() {
    return format_utc(std::time(nullptr), L"%Y%m%d-%H%M%S");
}

bool db_is_guid(const std::wstring& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const wchar_t c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != L'-') return false;
            continue;
        }
        const bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!hex) return false;
    }
    return true;
}

std::wstring db_normalize_guid(const std::wstring& value) {
    std::wstring out = value;
    if (!out.empty() && out.front() == L'{') out.erase(out.begin());
    if (!out.empty() && out.back() == L'}') out.pop_back();
    for (auto& c : out) {
        if (c >= L'A' && c <= L'F') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

std::string db_json_int_array(const std::vector<int>& values) {
    json::Array arr;
    arr.reserve(values.size());
    for (int v : values) arr.emplace_back(v);
    return json::serialize(json::Value(std::move(arr)));
}

std::string db_json_string_array(const std::vector<std::wstring>& values) {
    json::Array arr;
    arr.reserve(values.size());
    for (const auto& v : values) arr.emplace_back(ws2u(v));
    return json::serialize(json::Value(std::move(arr)));
}

std::vector<int> db_parse_int_array(const char* text) {
    std::vector<int> result;
    if (!text || !*text) return result;
    try {
        const auto parsed = json::parse(text);
        if (!parsed.is_array()) return result;
        for (const auto& item : parsed.as_array()) {
            if (item.is_number()) result.push_back(item.as_int());
        }
    } catch (...) {
        // Повреждённое значение трактуем как пустой список: планировщик не должен падать.
    }
    return result;
}

std::vector<std::wstring> db_parse_string_array(const char* text) {
    std::vector<std::wstring> result;
    if (!text || !*text) return result;
    try {
        const auto parsed = json::parse(text);
        if (!parsed.is_array()) return result;
        for (const auto& item : parsed.as_array()) {
            if (item.is_string()) result.push_back(u2ws(item.as_string()));
        }
    } catch (...) {
        // См. комментарий выше.
    }
    return result;
}
