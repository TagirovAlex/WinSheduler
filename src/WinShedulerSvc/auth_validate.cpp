#include "auth_validate.h"
#include <cwctype>
#include <vector>

namespace auth {

namespace {

bool is_sep(wchar_t c) { return c == L'/' || c == L'\\'; }

bool is_digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

bool all_digits(const std::wstring& s, std::size_t from, std::size_t count) {
    if (from + count > s.size()) return false;
    for (std::size_t i = from; i < from + count; ++i) {
        if (!is_digit(s[i])) return false;
    }
    return true;
}

int to_int(const std::wstring& s, std::size_t from, std::size_t count) {
    int v = 0;
    for (std::size_t i = from; i < from + count; ++i) v = v * 10 + (s[i] - L'0');
    return v;
}

bool contains_control(const std::wstring& s) {
    for (wchar_t c : s) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == 0) return true;
    }
    return false;
}

bool is_under_temp(const std::wstring& path) {
    wchar_t temp[MAX_PATH + 1] = {};
    DWORD len = GetTempPathW(MAX_PATH, temp);
    if (len == 0 || len > MAX_PATH) return false;
    std::wstring prefix(temp, len);
    while (!prefix.empty() && is_sep(prefix.back())) prefix.pop_back();
    if (prefix.empty()) return false;
    if (path.size() <= prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(path[i]) != std::towlower(prefix[i])) return false;
    }
    return is_sep(path[prefix.size()]);
}

// Разбор строки пути: буква диска, разделитель, сегменты.
bool split_local_path(const std::wstring& path, std::vector<std::wstring>& segments) {
    segments.clear();
    if (path.size() < 3) return false;
    if (!std::iswalpha(static_cast<wint_t>(path[0]))) return false;
    if (path[1] != L':') return false;
    if (!is_sep(path[2])) return false;
    // Второе двоеточие — альтернативный поток (file.txt:hidden) или мусор.
    if (path.find(L':', 2) != std::wstring::npos) return false;

    std::wstring current;
    for (std::size_t i = 3; i < path.size(); ++i) {
        wchar_t c = path[i];
        if (is_sep(c)) {
            if (current.empty()) return false; // пустой сегмент
            if (current == L"." || current == L"..") return false;
            segments.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        if (current == L"." || current == L"..") return false;
        segments.push_back(current);
    }
    return true;
}

} // namespace

bool is_guid(const std::wstring& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != L'-') return false;
        } else if (!std::iswxdigit(static_cast<wint_t>(value[i]))) {
            return false;
        }
    }
    return true;
}

bool is_local_absolute_path(const std::wstring& path) {
    if (path.empty() || contains_control(path)) return false;
    if (path.find(L"//") != std::wstring::npos || path.find(L"\\\\") != std::wstring::npos) {
        // Двойной разделитель в начале — UNC/device path, внутри — пустой сегмент.
        return false;
    }
    std::vector<std::wstring> segments;
    return split_local_path(path, segments);
}

bool is_valid_working_directory(const std::wstring& path) {
    if (path.empty()) return true; // пусто — наследовать каталог процесса
    if (!is_local_absolute_path(path)) return false;
    std::vector<std::wstring> segments;
    if (!split_local_path(path, segments)) return false;
    // Корень диска рабочим каталогом не назначаем (SECURITY.md раздел 5).
    return !segments.empty();
}

bool is_hhmm(const std::wstring& value) {
    if (value.size() != 5 || value[2] != L':') return false;
    if (!all_digits(value, 0, 2) || !all_digits(value, 3, 2)) return false;
    int h = to_int(value, 0, 2);
    int m = to_int(value, 3, 2);
    return h <= 23 && m <= 59;
}

bool is_yyyy_mm_dd(const std::wstring& value) {
    if (value.size() != 10 || value[4] != L'-' || value[7] != L'-') return false;
    if (!all_digits(value, 0, 4) || !all_digits(value, 5, 2) || !all_digits(value, 8, 2)) return false;
    int year = to_int(value, 0, 4);
    int month = to_int(value, 5, 2);
    int day = to_int(value, 8, 2);
    return year >= 1970 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool is_yyyy_mm_dd_thh_mm(const std::wstring& value) {
    if (value.size() != 16 || value[10] != L'T') return false;
    if (!is_yyyy_mm_dd(value.substr(0, 10))) return false;
    return is_hhmm(value.substr(11));
}

bool validate_task(const TaskDefinition& task, bool is_admin, std::wstring& error) {
    if (task.name.empty()) {
        error = L"Не задано имя задачи";
        return false;
    }
    if (task.name.size() > 256) {
        error = L"Имя задачи длиннее 256 символов";
        return false;
    }
    if (!is_local_absolute_path(task.program_path)) {
        error = L"Программа должна быть локальным абсолютным путём";
        return false;
    }
    if (!is_admin && is_under_temp(task.program_path)) {
        error = L"Путь во временном каталоге запрещён для непривилегированной задачи";
        return false;
    }
    if (!is_valid_working_directory(task.working_directory)) {
        error = L"Рабочий каталог должен быть локальным и не быть корнем диска";
        return false;
    }
    if (task.session_mode == SessionMode::ActiveUser) {
        error = L"Режим ActiveUser в v1 не поддерживается";
        return false;
    }
    if (task.retry_count < 0 || task.retry_count > 10) {
        error = L"retryCount вне диапазона 0…10";
        return false;
    }
    if (task.timeout_minutes.has_value() &&
        (task.timeout_minutes.value() < 0 || task.timeout_minutes.value() > 10080)) {
        error = L"timeoutMinutes вне диапазона 0…10080";
        return false;
    }
    if (task.max_history_records < 1 || task.max_history_records > 10000) {
        error = L"maxHistoryRecords вне диапазона 1…10000";
        return false;
    }
    if (task.run_as_user.has_value()) {
        if (task.run_as_user->user_name.empty()) {
            error = L"runAsUser задан пустым";
            return false;
        }
        if (!is_admin) {
            error = L"RunAs доступен только администратору";
            return false;
        }
    }
    return true;
}

bool validate_time_window(const TimeWindow& w, std::wstring& error) {
    if (w.kind == TriggerKind::Once) {
        if (w.specific_dates.empty() || w.exact_times.empty()) {
            error = L"Once: нужны specificDates и exactTimes";
            return false;
        }
    } else if (w.kind == TriggerKind::Daily) {
        if (w.exact_times.empty()) {
            error = L"Daily: нужен непустой exactTimes";
            return false;
        }
    } else if (w.kind == TriggerKind::Weekly) {
        if (w.days_of_week.empty() || w.exact_times.empty()) {
            error = L"Weekly: нужны daysOfWeek и exactTimes";
            return false;
        }
    } else if (w.kind == TriggerKind::Monthly) {
        if (w.days_of_month.empty() || w.exact_times.empty()) {
            error = L"Monthly: нужны daysOfMonth и exactTimes";
            return false;
        }
    } else if (w.kind == TriggerKind::Interval) {
        if (!w.start_time.has_value() || !w.end_time.has_value() ||
            !is_hhmm(*w.start_time) || !is_hhmm(*w.end_time)) {
            error = L"Interval: нужны корректные startTime и endTime";
            return false;
        }
        if (*w.start_time >= *w.end_time) {
            error = L"Interval: startTime должен быть меньше endTime";
            return false;
        }
        if (!w.repeat_interval_minutes.has_value() ||
            *w.repeat_interval_minutes < 1 || *w.repeat_interval_minutes > 1440) {
            error = L"Interval: repeatIntervalMinutes вне диапазона 1…1440";
            return false;
        }
    } else {
        error = L"Неизвестный тип триггера";
        return false;
    }

    if (w.day_interval < 1 || w.day_interval > 365) {
        error = L"dayInterval вне диапазона 1…365";
        return false;
    }
    if (w.week_interval < 1 || w.week_interval > 52) {
        error = L"weekInterval вне диапазона 1…52";
        return false;
    }
    if (w.month_interval < 1 || w.month_interval > 12) {
        error = L"monthInterval вне диапазона 1…12";
        return false;
    }
    if (w.start_date.has_value() && !w.start_date->empty() && !is_yyyy_mm_dd(*w.start_date)) {
        error = L"startDate должен быть в формате yyyy-MM-dd";
        return false;
    }
    if (w.end_date.has_value() && !w.end_date->empty() && !is_yyyy_mm_dd(*w.end_date)) {
        error = L"endDate должен быть в формате yyyy-MM-dd";
        return false;
    }
    if (w.repeat_until.has_value() && !w.repeat_until->empty() && !is_yyyy_mm_dd_thh_mm(*w.repeat_until)) {
        error = L"repeatUntil должен быть в формате yyyy-MM-ddTHH:mm";
        return false;
    }
    for (const auto& t : w.exact_times) {
        if (!is_hhmm(t)) {
            error = L"Время должно быть в формате HH:mm";
            return false;
        }
    }
    for (const auto& d : w.specific_dates) {
        if (!is_yyyy_mm_dd(d)) {
            error = L"Дата должна быть в формате yyyy-MM-dd";
            return false;
        }
    }
    for (int d : w.days_of_week) {
        if (d < 0 || d > 6) {
            error = L"daysOfWeek вне диапазона 0…6";
            return false;
        }
    }
    for (int d : w.days_of_month) {
        if (d < 1 || d > 31) {
            error = L"daysOfMonth вне диапазона 1…31";
            return false;
        }
    }
    return true;
}

std::wstring sanitize_line(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == 0) out += L' ';
        else out += c;
    }
    return out;
}

} // namespace auth
