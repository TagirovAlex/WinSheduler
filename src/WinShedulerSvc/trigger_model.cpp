#include "trigger_model.h"

#include <cstdio>
#include <ctime>

namespace trigger {

namespace {

bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool all_digits(const std::wstring& text, size_t from, size_t count) {
    if (from + count > text.size()) return false;
    for (size_t i = from; i < from + count; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
    }
    return true;
}

int to_int(const std::wstring& text, size_t from, size_t count) {
    int value = 0;
    for (size_t i = from; i < from + count; ++i) value = value * 10 + (text[i] - L'0');
    return value;
}

std::optional<int64_t> parse_date_days(const std::optional<std::wstring>& text) {
    if (!text.has_value()) return std::nullopt;
    LocalDateTime value;
    if (!parse_iso_date(*text, value)) return std::nullopt;
    return days_from_civil(value.year, value.month, value.day);
}

} // namespace

// Ограничение повторов repeatUntil. Поддерживается полное "yyyy-MM-ddTHH:mm"
// и легаси-значение "HH:mm" (ограничение в пределах суток).
bool slot_within_repeat_until(const TimeWindow& window, const LocalDateTime& slot) {
    if (!window.repeat_until.has_value() || window.repeat_until->empty()) return true;

    LocalDateTime limit;
    if (parse_iso_local(*window.repeat_until, limit)) {
        const int64_t slot_key = days_from_civil(slot.year, slot.month, slot.day) * 1440 + slot.hour * 60 + slot.minute;
        const int64_t limit_key = days_from_civil(limit.year, limit.month, limit.day) * 1440 + limit.hour * 60 + limit.minute;
        return slot_key <= limit_key;
    }

    int minutes = 0;
    if (parse_hhmm(*window.repeat_until, minutes)) {
        return slot.hour * 60 + slot.minute <= minutes;
    }

    // Невалидное значение отсекается валидацией; в рантайме игнорируем.
    return true;
}

int64_t days_from_civil(int year, int month, int day) {
    // Алгоритм Говарда Хиннанта: дни от 1970-01-01.
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(day - 1);
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    year = y + (m <= 2u ? 1 : 0);
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

int weekday_of_days(int64_t days) {
    // 1970-01-01 — четверг (4), 0 = воскресенье.
    const int64_t value = (days + 4) % 7;
    return static_cast<int>(value < 0 ? value + 7 : value);
}

int days_in_month(int year, int month) {
    static const int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap(year)) return 29;
    return kDays[month - 1];
}

bool parse_hhmm(const std::wstring& text, int& minutes_of_day) {
    if (text.size() != 5 || text[2] != L':') return false;
    if (!all_digits(text, 0, 2) || !all_digits(text, 3, 2)) return false;
    const int hour = to_int(text, 0, 2);
    const int minute = to_int(text, 3, 2);
    if (hour > 23 || minute > 59) return false;
    minutes_of_day = hour * 60 + minute;
    return true;
}

std::wstring format_hhmm(int minutes_of_day) {
    if (minutes_of_day < 0) minutes_of_day = 0;
    if (minutes_of_day > 1439) minutes_of_day = 1439;
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02d:%02d", minutes_of_day / 60, minutes_of_day % 60);
    return buf;
}

bool parse_iso_date(const std::wstring& text, LocalDateTime& out) {
    if (text.size() != 10 || text[4] != L'-' || text[7] != L'-') return false;
    if (!all_digits(text, 0, 4) || !all_digits(text, 5, 2) || !all_digits(text, 8, 2)) return false;
    const int year = to_int(text, 0, 4);
    const int month = to_int(text, 5, 2);
    const int day = to_int(text, 8, 2);
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > days_in_month(year, month)) return false;
    out.year = year;
    out.month = month;
    out.day = day;
    out.hour = 0;
    out.minute = 0;
    out.second = 0;
    return true;
}

bool parse_iso_local(const std::wstring& text, LocalDateTime& out) {
    if (text.size() != 16 && text.size() != 19) return false;
    if (text[10] != L'T' || text[13] != L':') return false;
    LocalDateTime date;
    if (!parse_iso_date(text.substr(0, 10), date)) return false;
    if (!all_digits(text, 11, 2) || !all_digits(text, 14, 2)) return false;
    const int hour = to_int(text, 11, 2);
    const int minute = to_int(text, 14, 2);
    if (hour > 23 || minute > 59) return false;
    int second = 0;
    if (text.size() == 19) {
        if (text[16] != L':' || !all_digits(text, 17, 2)) return false;
        second = to_int(text, 17, 2);
        if (second > 59) return false;
    }
    out = date;
    out.hour = hour;
    out.minute = minute;
    out.second = second;
    return true;
}

std::wstring format_iso_date(const LocalDateTime& value) {
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", value.year, value.month, value.day);
    return buf;
}

std::wstring format_slot(const LocalDateTime& value) {
    wchar_t buf[24] = {};
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d", value.year, value.month, value.day, value.hour, value.minute);
    return buf;
}

std::wstring format_utc_iso(std::chrono::system_clock::time_point utc) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(utc);
    std::tm tm = {};
    gmtime_s(&tm, &seconds);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

bool validate_time_window(const TimeWindow& window, std::wstring& error) {
    // Общие проверки для всех типов.
    for (const auto& time : window.exact_times) {
        int minutes = 0;
        if (!parse_hhmm(time, minutes)) {
            error = L"Неверный формат времени: ожидается HH:mm";
            return false;
        }
    }

    if (window.start_time.has_value()) {
        int minutes = 0;
        if (!parse_hhmm(*window.start_time, minutes)) {
            error = L"Неверный формат startTime: ожидается HH:mm";
            return false;
        }
    }
    if (window.end_time.has_value()) {
        int minutes = 0;
        if (!parse_hhmm(*window.end_time, minutes)) {
            error = L"Неверный формат endTime: ожидается HH:mm";
            return false;
        }
    }

    LocalDateTime date;
    if (window.start_date.has_value() && !parse_iso_date(*window.start_date, date)) {
        error = L"Неверный формат startDate: ожидается yyyy-MM-dd";
        return false;
    }
    if (window.end_date.has_value() && !parse_iso_date(*window.end_date, date)) {
        error = L"Неверный формат endDate: ожидается yyyy-MM-dd";
        return false;
    }
    if (window.start_date.has_value() && window.end_date.has_value()) {
        LocalDateTime start;
        LocalDateTime end;
        parse_iso_date(*window.start_date, start);
        parse_iso_date(*window.end_date, end);
        if (days_from_civil(start.year, start.month, start.day) >
            days_from_civil(end.year, end.month, end.day)) {
            error = L"startDate не может быть позже endDate";
            return false;
        }
    }

    if (window.repeat_until.has_value() && !window.repeat_until->empty()) {
        LocalDateTime limit;
        int minutes = 0;
        if (!parse_iso_local(*window.repeat_until, limit) && !parse_hhmm(*window.repeat_until, minutes)) {
            error = L"Неверный формат repeatUntil: ожидается yyyy-MM-ddTHH:mm";
            return false;
        }
    }

    for (const auto& date_text : window.specific_dates) {
        if (!parse_iso_date(date_text, date)) {
            error = L"Неверный формат даты в specificDates: ожидается yyyy-MM-dd";
            return false;
        }
    }

    switch (window.kind) {
    case TriggerKind::Once:
        if (window.specific_dates.empty()) {
            error = L"Для Once требуется непустой список specificDates";
            return false;
        }
        if (window.exact_times.empty()) {
            error = L"Для Once требуется непустой список exactTimes";
            return false;
        }
        break;

    case TriggerKind::Daily:
        if (window.exact_times.empty()) {
            error = L"Для Daily требуется непустой список exactTimes";
            return false;
        }
        if (window.day_interval < 1 || window.day_interval > 365) {
            error = L"dayInterval должен быть в диапазоне 1…365";
            return false;
        }
        break;

    case TriggerKind::Weekly:
        if (window.days_of_week.empty()) {
            error = L"Для Weekly требуется непустой список daysOfWeek";
            return false;
        }
        for (int day : window.days_of_week) {
            if (day < 0 || day > 6) {
                error = L"daysOfWeek должен содержать значения 0…6";
                return false;
            }
        }
        if (window.exact_times.empty()) {
            error = L"Для Weekly требуется непустой список exactTimes";
            return false;
        }
        if (window.week_interval < 1 || window.week_interval > 52) {
            error = L"weekInterval должен быть в диапазоне 1…52";
            return false;
        }
        break;

    case TriggerKind::Monthly:
        if (window.days_of_month.empty()) {
            error = L"Для Monthly требуется непустой список daysOfMonth";
            return false;
        }
        for (int day : window.days_of_month) {
            if (day < 1 || day > 31) {
                error = L"daysOfMonth должен содержать значения 1…31";
                return false;
            }
        }
        if (window.exact_times.empty()) {
            error = L"Для Monthly требуется непустой список exactTimes";
            return false;
        }
        if (window.month_interval < 1 || window.month_interval > 12) {
            error = L"monthInterval должен быть в диапазоне 1…12";
            return false;
        }
        break;

    case TriggerKind::Interval: {
        if (!window.start_time.has_value() || !window.end_time.has_value()) {
            error = L"Для Interval требуются startTime и endTime";
            return false;
        }
        if (!window.repeat_interval_minutes.has_value()) {
            error = L"Для Interval требуется repeatIntervalMinutes";
            return false;
        }
        int start_minutes = 0;
        int end_minutes = 0;
        parse_hhmm(*window.start_time, start_minutes);
        parse_hhmm(*window.end_time, end_minutes);
        if (start_minutes >= end_minutes) {
            error = L"startTime должен быть меньше endTime";
            return false;
        }
        if (*window.repeat_interval_minutes < 1 || *window.repeat_interval_minutes > 1440) {
            error = L"repeatIntervalMinutes должен быть в диапазоне 1…1440";
            return false;
        }
        break;
    }

    default:
        error = L"Неизвестный тип триггера";
        return false;
    }

    return true;
}

bool validate_session_mode(SessionMode mode, std::wstring& error) {
    if (mode == SessionMode::Headless) return true;
    // ActiveUser появится вместе с отдельным ADR (интерактивный режим).
    error = L"Режим ActiveUser в этой версии не поддерживается";
    return false;
}

bool validate_task(const TaskDefinition& task, std::wstring& error) {
    if (task.name.empty()) {
        error = L"Имя задачи обязательно";
        return false;
    }
    if (task.program_path.empty()) {
        error = L"Путь к программе обязателен";
        return false;
    }
    if (!validate_session_mode(task.session_mode, error)) return false;
    if (task.retry_count < 0 || task.retry_count > 10) {
        error = L"retryCount должен быть в диапазоне 0…10";
        return false;
    }
    if (task.timeout_minutes.has_value() && (*task.timeout_minutes < 0 || *task.timeout_minutes > 10080)) {
        error = L"timeoutMinutes должен быть в диапазоне 0…10080";
        return false;
    }
    if (task.max_history_records < 1 || task.max_history_records > 10000) {
        error = L"maxHistoryRecords должен быть в диапазоне 1…10000";
        return false;
    }
    return true;
}

int trigger_kind_to_db(TriggerKind kind) {
    return static_cast<int>(kind);
}

TriggerKind trigger_kind_from_db(int value) {
    switch (value) {
    case 0: return TriggerKind::Once;
    case 2: return TriggerKind::Weekly;
    case 3: return TriggerKind::Monthly;
    case 4: return TriggerKind::Interval;
    case 1:
    default: return TriggerKind::Daily;
    }
}

int trigger_kind_to_legacy_type(TriggerKind kind) {
    return kind == TriggerKind::Interval ? 1 : 0;
}

TriggerKind trigger_kind_from_legacy_type(int legacy_type, bool has_days_of_month, bool has_days_of_week) {
    if (legacy_type == 1) return TriggerKind::Interval;
    if (has_days_of_month) return TriggerKind::Monthly;
    if (has_days_of_week) return TriggerKind::Weekly;
    return TriggerKind::Daily;
}

std::string int_array_to_json(const std::vector<int>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ",";
        out += std::to_string(values[i]);
    }
    out += "]";
    return out;
}

std::vector<int> int_array_from_json(const std::string& text) {
    std::vector<int> values;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '-' || (text[i] >= '0' && text[i] <= '9')) {
            bool negative = text[i] == '-';
            if (negative) ++i;
            int value = 0;
            bool has_digits = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                value = value * 10 + (text[i] - '0');
                has_digits = true;
                ++i;
            }
            if (has_digits) values.push_back(negative ? -value : value);
            continue;
        }
        ++i;
    }
    return values;
}

std::string string_array_to_json(const std::vector<std::wstring>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ",";
        out += "\"";
        for (char c : ws2u(values[i])) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
    }
    out += "]";
    return out;
}

std::vector<std::wstring> string_array_from_json(const std::string& text) {
    std::vector<std::wstring> values;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '"') {
            ++i;
            continue;
        }
        ++i;
        std::string item;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            item += text[i];
            ++i;
        }
        if (i < text.size()) ++i;
        values.push_back(u2ws(item));
    }
    return values;
}

} // namespace trigger
