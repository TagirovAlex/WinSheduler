#pragma once
#include "trigger_model.h"

// Часы с инъекцией: позволяют тестировать расписание без реального времени.
class IClock {
public:
    virtual ~IClock() = default;
    // Текущий момент в UTC (для событий и last_fired_at).
    virtual std::chrono::system_clock::time_point now_utc() const = 0;
    // Перевод UTC в локальное настенное время (для расписания).
    virtual LocalDateTime to_local(std::chrono::system_clock::time_point utc) const = 0;
    // Перевод локального настенного времени в UTC.
    virtual std::chrono::system_clock::time_point from_local(const LocalDateTime& local) const = 0;
};

// Системные часы: локальное время ОС, политика DST — системная.
class SystemClock final : public IClock {
public:
    std::chrono::system_clock::time_point now_utc() const override;
    LocalDateTime to_local(std::chrono::system_clock::time_point utc) const override;
    std::chrono::system_clock::time_point from_local(const LocalDateTime& local) const override;
};

// Чистый движок расписания: без БД, без процессов, только арифметика времени.
namespace trigger {

// Окно догоняющих запусков: слот срабатывает, если он попал в последние N минут.
// Защищает от пропуска при задержке тика и ограничивает «шторм» после простоя.
inline constexpr int CATCH_UP_MINUTES = 2;

// Горизонт поиска next_run: 10 лет.
inline constexpr int NEXT_RUN_HORIZON_DAYS = 366 * 10;

// true, если дата подходит под шаблон окна (без учёта времени суток).
bool date_matches(const TimeWindow& window, const LocalDateTime& date);

// Минуты суток (0..1439), в которые окно срабатывает в указанную дату.
// Пустой список означает, что дата не подходит.
std::vector<int> slot_minutes(const TimeWindow& window, const LocalDateTime& date);

// true, если текущая минута является слотом окна.
bool is_slot(const TimeWindow& window, const LocalDateTime& now);

// Слоты в интервале (now - catch_up_minutes; now]. Порядок — по возрастанию.
// Повторный запуск одного слота отсекается хранилищем trigger_state.
std::vector<LocalDateTime> due_slots(const TimeWindow& window, const LocalDateTime& now, int catch_up_minutes);

// Ближайший слот строго после after. nullopt, если слотов нет на горизонте.
std::optional<LocalDateTime> next_run(const TimeWindow& window, const LocalDateTime& after);

} // namespace trigger
