#pragma once
#include "config.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Локальное настенное время без привязки к UTC.
// Слоты расписания задаются с точностью до минуты, секунды нужны для форматирования.
struct LocalDateTime {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

// Модель триггера v2: разбор, валидация и маппинг в БД.
// SQL-привязка колонок остаётся в ./src/WinShedulerSvc/database.cpp (зона трека C),
// здесь — только чистые функции, пригодные для тестов без БД.
namespace trigger {

// --- Календарная арифметика (без mktime, чтобы не зависеть от DST) ---

// Количество дней от 1970-01-01 до указанной даты (может быть отрицательным).
int64_t days_from_civil(int year, int month, int day);
// Обратное преобразование дней от эпохи в календарную дату.
void civil_from_days(int64_t days, int& year, int& month, int& day);
// День недели: 0 = воскресенье, 6 = суббота (как tm_wday и DB.md).
int weekday_of_days(int64_t days);
// Число дней в месяце с учётом високосного года.
int days_in_month(int year, int month);

// --- Разбор и форматирование ---

// Строгий разбор "HH:mm" (24 часа). minutes_of_day = 0..1439.
bool parse_hhmm(const std::wstring& text, int& minutes_of_day);
// Форматирование минут суток в "HH:mm".
std::wstring format_hhmm(int minutes_of_day);
// Строгий разбор "yyyy-MM-dd".
bool parse_iso_date(const std::wstring& text, LocalDateTime& out);
// Разбор локального "yyyy-MM-ddTHH:mm" или "yyyy-MM-ddTHH:mm:ss".
bool parse_iso_local(const std::wstring& text, LocalDateTime& out);
// Форматирование даты "yyyy-MM-dd".
std::wstring format_iso_date(const LocalDateTime& value);
// Ключ слота "yyyy-MM-ddTHH:mm" (локальное время, см. DB.md).
std::wstring format_slot(const LocalDateTime& value);
// Время события в UTC ISO 8601 с "Z" (см. DB.md).
std::wstring format_utc_iso(std::chrono::system_clock::time_point utc);
// Ограничение repeatUntil для конкретного слота (полное время или легаси "HH:mm").
bool slot_within_repeat_until(const TimeWindow& window, const LocalDateTime& slot);

// Восстанавливает TriggerKind, если колонка kind ещё не сохраняется схемой v1
// (до C-2). Использует легаси-правила из DB.md, раздел 4.
TimeWindow normalize_time_window(const TimeWindow& window);

// --- Валидация ---

// Проверка окна расписания по ./docs/contracts/IPC.md. error заполняется по-русски.
bool validate_time_window(const TimeWindow& window, std::wstring& error);
// Проверка задачи целиком (поля, enum'ы, session_mode). error заполняется по-русски.
bool validate_task(const TaskDefinition& task, std::wstring& error);
// session_mode=ActiveUser в v1 отклоняется, поле зарезервировано под будущий режим.
bool validate_session_mode(SessionMode mode, std::wstring& error);

// --- Маппинг в БД (см. ./docs/contracts/DB.md) ---

// time_windows.kind: 0 Once, 1 Daily, 2 Weekly, 3 Monthly, 4 Interval.
int trigger_kind_to_db(TriggerKind kind);
TriggerKind trigger_kind_from_db(int value);
// Легаси time_windows.type: 0 ExactTimes, 1 Interval (совместимость с v1).
int trigger_kind_to_legacy_type(TriggerKind kind);
// Восстановление kind для строк v1 при миграции (DB.md, раздел 4).
TriggerKind trigger_kind_from_legacy_type(int legacy_type, bool has_days_of_month, bool has_days_of_week);

// JSON-массивы для колонок days_of_week / days_of_month / specific_dates / exact_times.
std::string int_array_to_json(const std::vector<int>& values);
std::vector<int> int_array_from_json(const std::string& text);
std::string string_array_to_json(const std::vector<std::wstring>& values);
std::vector<std::wstring> string_array_from_json(const std::string& text);

} // namespace trigger
