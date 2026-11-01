#include "trigger_engine.h"

#include <algorithm>
#include <ctime>

namespace trigger {

namespace {

int minute_of_day(const LocalDateTime& value) {
    return value.hour * 60 + value.minute;
}

int64_t day_number(const LocalDateTime& value) {
    return days_from_civil(value.year, value.month, value.day);
}

// Ключ момента с точностью до минуты: удобно сравнивать слоты.
int64_t minute_key(const LocalDateTime& value) {
    return day_number(value) * 1440 + minute_of_day(value);
}

LocalDateTime at_day(const LocalDateTime& date, int minutes_of_day) {
    LocalDateTime result = date;
    result.hour = minutes_of_day / 60;
    result.minute = minutes_of_day % 60;
    result.second = 0;
    return result;
}

LocalDateTime shift_days(const LocalDateTime& value, int64_t days) {
    LocalDateTime result;
    civil_from_days(day_number(value) + days, result.year, result.month, result.day);
    result.hour = value.hour;
    result.minute = value.minute;
    result.second = value.second;
    return result;
}

LocalDateTime shift_minutes(const LocalDateTime& value, int minutes) {
    const int64_t key = minute_key(value) + minutes;
    const int64_t days = key >= 0 ? key / 1440 : (key - 1439) / 1440;
    const int remainder = static_cast<int>(key - days * 1440);
    LocalDateTime result;
    civil_from_days(days, result.year, result.month, result.day);
    result.hour = remainder / 60;
    result.minute = remainder % 60;
    result.second = value.second;
    return result;
}

bool contains_int(const std::vector<int>& values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

// День начала отсчёта интервалов: start_date, иначе 1970-01-01.
int64_t anchor_day(const TimeWindow& window) {
    if (window.start_date.has_value()) {
        LocalDateTime date;
        if (parse_iso_date(*window.start_date, date)) return day_number(date);
    }
    return 0;
}

// Понедельник недели, в которую попадает день (0 = воскресенье).
int64_t monday_of_week(int64_t day) {
    const int weekday = weekday_of_days(day);
    const int offset = (weekday == 0) ? -6 : (1 - weekday);
    return day + offset;
}

int64_t floor_div(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) --quotient;
    return quotient;
}

} // namespace

bool date_matches(const TimeWindow& window, const LocalDateTime& date) {
    const int64_t day = day_number(date);

    // Диапазон дат включительно действует для всех типов.
    if (window.start_date.has_value()) {
        LocalDateTime start;
        if (parse_iso_date(*window.start_date, start) && day < day_number(start)) return false;
    }
    if (window.end_date.has_value()) {
        LocalDateTime end;
        if (parse_iso_date(*window.end_date, end) && day > day_number(end)) return false;
    }

    switch (window.kind) {
    case TriggerKind::Once: {
        for (const auto& text : window.specific_dates) {
            LocalDateTime specific;
            if (parse_iso_date(text, specific) && day_number(specific) == day) return true;
        }
        return false;
    }

    case TriggerKind::Daily: {
        const int64_t anchor = anchor_day(window);
        if (day < anchor) return false;
        const int step = window.day_interval > 0 ? window.day_interval : 1;
        return (day - anchor) % step == 0;
    }

    case TriggerKind::Weekly: {
        if (window.days_of_week.empty()) return false;
        if (!contains_int(window.days_of_week, weekday_of_days(day))) return false;
        const int64_t anchor = monday_of_week(anchor_day(window));
        const int64_t week = floor_div(day - anchor, 7);
        if (week < 0) return false;
        const int step = window.week_interval > 0 ? window.week_interval : 1;
        return week % step == 0;
    }

    case TriggerKind::Monthly: {
        if (window.days_of_month.empty()) return false;
        if (!contains_int(window.days_of_month, date.day)) return false;
        int anchor_year = 1970;
        int anchor_month = 1;
        if (window.start_date.has_value()) {
            LocalDateTime start;
            if (parse_iso_date(*window.start_date, start)) {
                anchor_year = start.year;
                anchor_month = start.month;
            }
        }
        const int64_t index = static_cast<int64_t>(date.year - anchor_year) * 12 + (date.month - anchor_month);
        if (index < 0) return false;
        const int step = window.month_interval > 0 ? window.month_interval : 1;
        return index % step == 0;
    }

    case TriggerKind::Interval:
        // Для Interval даты ограничены только диапазоном startDate/endDate.
        return true;
    }

    return false;
}

std::vector<int> slot_minutes(const TimeWindow& window, const LocalDateTime& date) {
    std::vector<int> result;
    if (!date_matches(window, date)) return result;

    auto add_slot = [&](int minutes) {
        if (minutes < 0 || minutes > 1439) return;
        const LocalDateTime slot = at_day(date, minutes);
        if (!slot_within_repeat_until(window, slot)) return;
        if (!contains_int(result, minutes)) result.push_back(minutes);
    };

    if (window.kind == TriggerKind::Interval) {
        int start = 0;
        int end = 0;
        if (!window.start_time.has_value() || !window.end_time.has_value() ||
            !window.repeat_interval_minutes.has_value()) {
            return result;
        }
        if (!parse_hhmm(*window.start_time, start) || !parse_hhmm(*window.end_time, end)) return result;
        const int step = *window.repeat_interval_minutes;
        if (step < 1) return result;
        // Ровно один запуск на N минут, без повторного срабатывания внутри минуты.
        for (int minutes = start; minutes <= end; minutes += step) add_slot(minutes);
        return result;
    }

    for (const auto& text : window.exact_times) {
        int minutes = 0;
        if (parse_hhmm(text, minutes)) add_slot(minutes);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool is_slot(const TimeWindow& window, const LocalDateTime& now) {
    return contains_int(slot_minutes(window, now), minute_of_day(now));
}

std::vector<LocalDateTime> due_slots(const TimeWindow& window, const LocalDateTime& now, int catch_up_minutes) {
    std::vector<LocalDateTime> result;
    if (catch_up_minutes < 0) catch_up_minutes = 0;

    const LocalDateTime from = shift_minutes(now, -catch_up_minutes);
    const int64_t from_key = minute_key(from);
    const int64_t now_key = minute_key(now);
    const int64_t day_span = catch_up_minutes / 1440 + 1;

    for (int64_t offset = 0; offset <= day_span; ++offset) {
        const LocalDateTime date = shift_days(from, offset);
        for (int minutes : slot_minutes(window, date)) {
            const LocalDateTime slot = at_day(date, minutes);
            const int64_t key = minute_key(slot);
            if (key > from_key && key <= now_key) result.push_back(slot);
        }
    }

    std::sort(result.begin(), result.end(), [](const LocalDateTime& left, const LocalDateTime& right) {
        return minute_key(left) < minute_key(right);
    });
    return result;
}

std::optional<LocalDateTime> next_run(const TimeWindow& window, const LocalDateTime& after) {
    const int64_t start_day = day_number(after);
    const int64_t after_key = minute_key(after);

    for (int offset = 0; offset <= NEXT_RUN_HORIZON_DAYS; ++offset) {
        LocalDateTime date;
        civil_from_days(start_day + offset, date.year, date.month, date.day);
        for (int minutes : slot_minutes(window, date)) {
            const LocalDateTime slot = at_day(date, minutes);
            if (minute_key(slot) > after_key) return slot;
        }
    }
    return std::nullopt;
}

} // namespace trigger

std::chrono::system_clock::time_point SystemClock::now_utc() const {
    return std::chrono::system_clock::now();
}

LocalDateTime SystemClock::to_local(std::chrono::system_clock::time_point utc) const {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(utc);
    std::tm tm = {};
    localtime_s(&tm, &seconds);
    LocalDateTime result;
    result.year = tm.tm_year + 1900;
    result.month = tm.tm_mon + 1;
    result.day = tm.tm_mday;
    result.hour = tm.tm_hour;
    result.minute = tm.tm_min;
    result.second = tm.tm_sec;
    return result;
}

std::chrono::system_clock::time_point SystemClock::from_local(const LocalDateTime& local) const {
    std::tm tm = {};
    tm.tm_year = local.year - 1900;
    tm.tm_mon = local.month - 1;
    tm.tm_mday = local.day;
    tm.tm_hour = local.hour;
    tm.tm_min = local.minute;
    tm.tm_sec = local.second;
    tm.tm_isdst = -1;
    const std::time_t seconds = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(seconds);
}
