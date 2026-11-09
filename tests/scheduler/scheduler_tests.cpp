// Тесты планировщика (трек A): модель триггера, движок расписания, fake clock.
// Собственный минимальный харнесс: без внешних зависимостей и NuGet.
// Сборка и запуск: ./tests/scheduler/run.ps1

#include "trigger_engine.h"
#include "trigger_model.h"
#include "database.h"
#include "process_runner.h"
#include "scheduler.h"
#include "trigger_state.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("  ОШИБКА (строка %d): %s\n", line, expression);
}

#define CHECK(condition) check((condition), #condition, __LINE__)

LocalDateTime dt(int year, int month, int day, int hour, int minute) {
    LocalDateTime value;
    value.year = year;
    value.month = month;
    value.day = day;
    value.hour = hour;
    value.minute = minute;
    return value;
}

TimeWindow window_of(TriggerKind kind) {
    TimeWindow window;
    window.kind = kind;
    return window;
}

// Часы с фиксированным временем: детерминированные тесты без реального времени.
class FakeClock final : public IClock {
public:
    FakeClock(LocalDateTime local, std::chrono::system_clock::time_point utc) : local_(local), utc_(utc) {}

    std::chrono::system_clock::time_point now_utc() const override { return utc_; }
    LocalDateTime to_local(std::chrono::system_clock::time_point) const override { return local_; }
    std::chrono::system_clock::time_point from_local(const LocalDateTime&) const override { return utc_; }

    void advance(int minutes) {
        utc_ += std::chrono::minutes(minutes);
        local_.hour += minutes / 60;
        local_.minute += minutes % 60;
    }

private:
    LocalDateTime local_;
    std::chrono::system_clock::time_point utc_;
};

void test_calendar() {
    std::printf("Календарная арифметика\n");
    CHECK(trigger::days_from_civil(1970, 1, 1) == 0);
    CHECK(trigger::weekday_of_days(trigger::days_from_civil(1970, 1, 1)) == 4); // четверг
    CHECK(trigger::weekday_of_days(trigger::days_from_civil(2026, 11, 1)) == 0); // воскресенье
    CHECK(trigger::days_in_month(2024, 2) == 29);
    CHECK(trigger::days_in_month(2026, 2) == 28);
    CHECK(trigger::days_in_month(2026, 12) == 31);

    int year = 0;
    int month = 0;
    int day = 0;
    trigger::civil_from_days(trigger::days_from_civil(2026, 11, 1), year, month, day);
    CHECK(year == 2026 && month == 11 && day == 1);
    trigger::civil_from_days(trigger::days_from_civil(1969, 12, 31), year, month, day);
    CHECK(year == 1969 && month == 12 && day == 31);
}

void test_parsing() {
    std::printf("Разбор и форматирование времени\n");
    int minutes = -1;
    CHECK(trigger::parse_hhmm(L"09:30", minutes) && minutes == 570);
    CHECK(trigger::parse_hhmm(L"00:00", minutes) && minutes == 0);
    CHECK(trigger::parse_hhmm(L"23:59", minutes) && minutes == 1439);
    CHECK(!trigger::parse_hhmm(L"24:00", minutes));
    CHECK(!trigger::parse_hhmm(L"9:30", minutes));
    CHECK(!trigger::parse_hhmm(L"09-30", minutes));
    CHECK(!trigger::parse_hhmm(L"", minutes));
    CHECK(trigger::format_hhmm(570) == L"09:30");
    CHECK(trigger::format_hhmm(0) == L"00:00");

    LocalDateTime value;
    CHECK(trigger::parse_iso_date(L"2026-11-01", value));
    CHECK(value.year == 2026 && value.month == 11 && value.day == 1);
    CHECK(!trigger::parse_iso_date(L"2026-02-30", value));
    CHECK(!trigger::parse_iso_date(L"2026-13-01", value));
    CHECK(!trigger::parse_iso_date(L"2026-1-1", value));

    CHECK(trigger::parse_iso_local(L"2026-11-01T09:30", value));
    CHECK(value.hour == 9 && value.minute == 30);
    CHECK(trigger::parse_iso_local(L"2026-11-01T09:30:45", value));
    CHECK(value.second == 45);
    CHECK(!trigger::parse_iso_local(L"2026-11-01 09:30", value));

    CHECK(trigger::format_iso_date(dt(2026, 11, 1, 0, 0)) == L"2026-11-01");
    CHECK(trigger::format_slot(dt(2026, 11, 1, 9, 5)) == L"2026-11-01T09:05");

    // События пишутся в UTC: 1970-01-01T00:00:00Z.
    const auto epoch = std::chrono::system_clock::from_time_t(0);
    CHECK(trigger::format_utc_iso(epoch) == L"1970-01-01T00:00:00Z");
}

void test_validation() {
    std::printf("Валидация триггеров\n");
    std::wstring error;

    {
        auto window = window_of(TriggerKind::Once);
        window.specific_dates = { L"2026-11-01" };
        window.exact_times = { L"09:00" };
        CHECK(trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Once);
        window.exact_times = { L"09:00" };
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Daily);
        window.exact_times = { L"02:00" };
        CHECK(trigger::validate_time_window(window, error));
        window.day_interval = 0;
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Weekly);
        window.days_of_week = { 1, 3, 5 };
        window.exact_times = { L"08:00" };
        CHECK(trigger::validate_time_window(window, error));
        window.days_of_week = { 7 };
        CHECK(!trigger::validate_time_window(window, error));
        window.days_of_week = { 1 };
        window.week_interval = 53;
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Monthly);
        window.days_of_month = { 1, 31 };
        window.exact_times = { L"08:00" };
        CHECK(trigger::validate_time_window(window, error));
        window.days_of_month = { 0 };
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Interval);
        window.start_time = L"09:00";
        window.end_time = L"10:00";
        window.repeat_interval_minutes = 20;
        CHECK(trigger::validate_time_window(window, error));
        window.repeat_interval_minutes = 0;
        CHECK(!trigger::validate_time_window(window, error));
        window.repeat_interval_minutes = 20;
        window.start_time = L"10:00";
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Daily);
        window.exact_times = { L"02:00" };
        window.start_date = L"2026-12-01";
        window.end_date = L"2026-11-01";
        CHECK(!trigger::validate_time_window(window, error));
    }
    {
        auto window = window_of(TriggerKind::Daily);
        window.exact_times = { L"2:00" };
        CHECK(!trigger::validate_time_window(window, error));
    }

    // session_mode=ActiveUser в v1 отклоняется.
    CHECK(trigger::validate_session_mode(SessionMode::Headless, error));
    CHECK(!trigger::validate_session_mode(SessionMode::ActiveUser, error));

    TaskDefinition task;
    task.name = L"Тест";
    task.program_path = L"d:/scripts/test.cmd";
    CHECK(trigger::validate_task(task, error));
    task.session_mode = SessionMode::ActiveUser;
    CHECK(!trigger::validate_task(task, error));
}

void test_mapping() {
    std::printf("Маппинг в БД\n");
    CHECK(trigger::trigger_kind_to_db(TriggerKind::Once) == 0);
    CHECK(trigger::trigger_kind_to_db(TriggerKind::Daily) == 1);
    CHECK(trigger::trigger_kind_to_db(TriggerKind::Weekly) == 2);
    CHECK(trigger::trigger_kind_to_db(TriggerKind::Monthly) == 3);
    CHECK(trigger::trigger_kind_to_db(TriggerKind::Interval) == 4);
    CHECK(trigger::trigger_kind_from_db(4) == TriggerKind::Interval);
    CHECK(trigger::trigger_kind_from_db(99) == TriggerKind::Daily);

    CHECK(trigger::trigger_kind_to_legacy_type(TriggerKind::Interval) == 1);
    CHECK(trigger::trigger_kind_to_legacy_type(TriggerKind::Daily) == 0);
    CHECK(trigger::trigger_kind_from_legacy_type(1, false, false) == TriggerKind::Interval);
    CHECK(trigger::trigger_kind_from_legacy_type(0, true, true) == TriggerKind::Monthly);
    CHECK(trigger::trigger_kind_from_legacy_type(0, false, true) == TriggerKind::Weekly);
    CHECK(trigger::trigger_kind_from_legacy_type(0, false, false) == TriggerKind::Daily);

    const std::vector<int> days = { 1, 3, 5 };
    CHECK(trigger::int_array_from_json(trigger::int_array_to_json(days)) == days);
    const std::vector<std::wstring> dates = { L"2026-11-01", L"2026-12-31" };
    CHECK(trigger::string_array_from_json(trigger::string_array_to_json(dates)) == dates);
    CHECK(trigger::int_array_from_json("[]").empty());
}

void test_daily() {
    std::printf("Daily: интервалы дней и точное время\n");
    auto window = window_of(TriggerKind::Daily);
    window.exact_times = { L"02:00", L"14:00" };
    window.start_date = L"2026-11-01";
    window.day_interval = 2;

    CHECK(trigger::date_matches(window, dt(2026, 11, 1, 0, 0)));
    CHECK(!trigger::date_matches(window, dt(2026, 11, 2, 0, 0)));
    CHECK(trigger::date_matches(window, dt(2026, 11, 3, 0, 0)));

    CHECK(trigger::is_slot(window, dt(2026, 11, 1, 2, 0)));
    CHECK(!trigger::is_slot(window, dt(2026, 11, 1, 2, 1)));
    CHECK(!trigger::is_slot(window, dt(2026, 11, 2, 2, 0)));

    const auto next = trigger::next_run(window, dt(2026, 11, 1, 2, 0));
    CHECK(next.has_value());
    CHECK(next.has_value() && trigger::format_slot(*next) == L"2026-11-01T14:00");

    const auto after_day = trigger::next_run(window, dt(2026, 11, 1, 14, 0));
    CHECK(after_day.has_value() && trigger::format_slot(*after_day) == L"2026-11-03T02:00");
}

void test_weekly() {
    std::printf("Weekly: дни недели и интервал недель\n");
    auto window = window_of(TriggerKind::Weekly);
    window.days_of_week = { 1 }; // понедельник
    window.exact_times = { L"08:00" };
    window.start_date = L"2026-11-02"; // понедельник
    window.week_interval = 2;

    CHECK(trigger::date_matches(window, dt(2026, 11, 2, 0, 0)));
    CHECK(!trigger::date_matches(window, dt(2026, 11, 9, 0, 0)));
    CHECK(trigger::date_matches(window, dt(2026, 11, 16, 0, 0)));
    CHECK(!trigger::date_matches(window, dt(2026, 11, 3, 0, 0))); // вторник

    const auto next = trigger::next_run(window, dt(2026, 11, 2, 8, 0));
    CHECK(next.has_value() && trigger::format_slot(*next) == L"2026-11-16T08:00");
}

void test_monthly() {
    std::printf("Monthly: дни месяца и интервал месяцев\n");
    auto window = window_of(TriggerKind::Monthly);
    window.days_of_month = { 31 };
    window.exact_times = { L"23:00" };
    window.month_interval = 1;

    CHECK(trigger::date_matches(window, dt(2026, 12, 31, 0, 0)));
    CHECK(!trigger::date_matches(window, dt(2026, 11, 30, 0, 0)));
    CHECK(!trigger::date_matches(window, dt(2026, 2, 28, 0, 0)));

    const auto next = trigger::next_run(window, dt(2026, 11, 1, 0, 0));
    CHECK(next.has_value() && trigger::format_slot(*next) == L"2026-12-31T23:00");

    auto every_other = window;
    every_other.month_interval = 2;
    every_other.start_date = L"2026-11-01";
    // Отсчёт от месяца start_date: ноябрь, январь, март, …
    CHECK(!trigger::date_matches(every_other, dt(2026, 12, 31, 0, 0)));
    CHECK(trigger::date_matches(every_other, dt(2027, 1, 31, 0, 0)));
    CHECK(!trigger::date_matches(every_other, dt(2027, 2, 28, 0, 0)));
    CHECK(trigger::date_matches(every_other, dt(2027, 3, 31, 0, 0)));
}

void test_once_and_range() {
    std::printf("Once: конкретные даты и диапазон дат\n");
    auto window = window_of(TriggerKind::Once);
    window.specific_dates = { L"2026-11-01", L"2026-12-31" };
    window.exact_times = { L"10:00" };

    CHECK(trigger::is_slot(window, dt(2026, 11, 1, 10, 0)));
    CHECK(!trigger::is_slot(window, dt(2026, 11, 2, 10, 0)));
    CHECK(trigger::is_slot(window, dt(2026, 12, 31, 10, 0)));

    const auto next = trigger::next_run(window, dt(2026, 11, 1, 10, 0));
    CHECK(next.has_value() && trigger::format_slot(*next) == L"2026-12-31T10:00");
    CHECK(!trigger::next_run(window, dt(2026, 12, 31, 10, 0)).has_value());

    auto ranged = window_of(TriggerKind::Daily);
    ranged.exact_times = { L"10:00" };
    ranged.start_date = L"2026-11-01";
    ranged.end_date = L"2026-11-03";
    CHECK(!trigger::date_matches(ranged, dt(2026, 10, 31, 0, 0)));
    CHECK(trigger::date_matches(ranged, dt(2026, 11, 1, 0, 0)));
    CHECK(trigger::date_matches(ranged, dt(2026, 11, 3, 0, 0)));
    CHECK(!trigger::date_matches(ranged, dt(2026, 11, 4, 0, 0)));
    CHECK(!trigger::next_run(ranged, dt(2026, 11, 3, 10, 0)).has_value());
}

void test_interval() {
    std::printf("Interval: ровно раз в N минут и repeatUntil\n");
    auto window = window_of(TriggerKind::Interval);
    window.start_time = L"09:00";
    window.end_time = L"10:00";
    window.repeat_interval_minutes = 20;

    const auto slots = trigger::slot_minutes(window, dt(2026, 11, 1, 0, 0));
    CHECK(slots.size() == 4);
    CHECK(slots.size() == 4 && slots[0] == 540 && slots[1] == 560 && slots[2] == 580 && slots[3] == 600);

    // Внутри одной минуты слот один: повторного срабатывания каждую секунду нет.
    CHECK(trigger::is_slot(window, dt(2026, 11, 1, 9, 20)));
    CHECK(!trigger::is_slot(window, dt(2026, 11, 1, 9, 21)));

    auto limited = window;
    limited.repeat_until = L"2026-11-01T09:30";
    const auto limited_slots = trigger::slot_minutes(limited, dt(2026, 11, 1, 0, 0));
    CHECK(limited_slots.size() == 2);
    CHECK(!trigger::is_slot(limited, dt(2026, 11, 1, 9, 40)));
    CHECK(trigger::date_matches(limited, dt(2026, 11, 2, 0, 0)));
    // На следующий день слоты после repeatUntil отсекаются.
    CHECK(trigger::slot_minutes(limited, dt(2026, 11, 2, 0, 0)).empty());

    // Легаси-значение "HH:mm" ограничивает слоты в пределах суток.
    auto legacy = window;
    legacy.repeat_until = L"09:30";
    CHECK(trigger::slot_minutes(legacy, dt(2026, 11, 1, 0, 0)).size() == 2);
}

void test_catch_up() {
    std::printf("Догоняющие запуски и коалесинг\n");
    auto daily = window_of(TriggerKind::Daily);
    daily.exact_times = { L"00:00" };

    const auto across_midnight = trigger::due_slots(daily, dt(2026, 11, 2, 0, 1), trigger::CATCH_UP_MINUTES);
    CHECK(across_midnight.size() == 1);
    CHECK(!across_midnight.empty() && trigger::format_slot(across_midnight[0]) == L"2026-11-02T00:00");

    // Слот двухминутной давности уже вне окна catch-up.
    CHECK(trigger::due_slots(daily, dt(2026, 11, 2, 0, 5), trigger::CATCH_UP_MINUTES).empty());

    auto interval = window_of(TriggerKind::Interval);
    interval.start_time = L"09:00";
    interval.end_time = L"10:00";
    interval.repeat_interval_minutes = 20;

    // Задержка тика на минуту не даёт двух запусков одного слота.
    const auto delayed = trigger::due_slots(interval, dt(2026, 11, 1, 9, 21), trigger::CATCH_UP_MINUTES);
    CHECK(delayed.size() == 1);
    CHECK(!delayed.empty() && trigger::format_slot(delayed[0]) == L"2026-11-01T09:20");

    // Fake clock: время двигается только по команде теста.
    FakeClock clock(dt(2026, 11, 1, 9, 0), std::chrono::system_clock::from_time_t(0));
    CHECK(clock.to_local(clock.now_utc()).minute == 0);
    clock.advance(20);
    CHECK(clock.to_local(clock.now_utc()).minute == 20);
}

void test_benchmark_1000_tasks() {
    std::printf("Бенчмарк: тик по 1000 задачам\n");

    std::vector<TimeWindow> windows;
    windows.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        auto window = window_of(TriggerKind::Daily);
        window.exact_times = { L"02:00", L"14:00" };
        window.day_interval = (i % 7) + 1;
        window.start_date = L"2026-01-01";
        windows.push_back(window);
    }

    const LocalDateTime now = dt(2026, 11, 1, 2, 0);
    const auto started = std::chrono::steady_clock::now();
    size_t fired = 0;
    for (const auto& window : windows) {
        fired += trigger::due_slots(window, now, trigger::CATCH_UP_MINUTES).size();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::printf("  тик: %.3f мс, сработало слотов: %zu\n", microseconds / 1000.0, fired);

    // Бюджет A-15: тик по 1000 задач укладывается в 50 мс.
    CHECK(microseconds < 50 * 1000);
    CHECK(fired > 0);
}

// --- Интеграционные тесты: БД, кэш расписания, fake runner ---

// Часы с изменяемым временем: тик и таймауты проверяются детерминированно.
class TestClock final : public IClock {
public:
    explicit TestClock(const LocalDateTime& start)
        : local_(start), utc_(std::chrono::system_clock::from_time_t(0)) {}

    std::chrono::system_clock::time_point now_utc() const override { return utc_; }
    LocalDateTime to_local(std::chrono::system_clock::time_point) const override { return local_; }
    std::chrono::system_clock::time_point from_local(const LocalDateTime&) const override { return utc_; }

    void advance(int minutes) {
        utc_ += std::chrono::minutes(minutes);
        int64_t day = trigger::days_from_civil(local_.year, local_.month, local_.day);
        int total = local_.hour * 60 + local_.minute + minutes;
        while (total >= 1440) { total -= 1440; ++day; }
        while (total < 0) { total += 1440; --day; }
        trigger::civil_from_days(day, local_.year, local_.month, local_.day);
        local_.hour = total / 60;
        local_.minute = total % 60;
    }

private:
    LocalDateTime local_;
    std::chrono::system_clock::time_point utc_;
};

// Поддельный раннер: процессов нет, состояние управляется тестом.
class FakeProcessRunner final : public IProcessRunner {
public:
    struct Process {
        bool running = true;
        DWORD exit_code = 0;
        bool terminated = false;
    };

    bool launch(const ProcessLaunchSpec& spec, RunningProcessHandle& out, std::wstring& error) override {
        last_spec = spec;
        if (fail_launch) {
            fail_launch = false;
            error = L"тестовый отказ запуска";
            return false;
        }
        ++launches;
        const DWORD pid = 1000 + static_cast<DWORD>(launches);
        processes[pid] = Process{};
        out.process = nullptr;
        out.job = nullptr;
        out.pid = pid;
        out.user_data = nullptr;
        return true;
    }

    ProcessStatus poll(const RunningProcessHandle& handle) override {
        ProcessStatus status;
        auto it = processes.find(handle.pid);
        if (it == processes.end() || !it->second.running) {
            status.exited = true;
            status.exit_code = it == processes.end() ? 0 : it->second.exit_code;
        }
        return status;
    }

    void terminate(RunningProcessHandle& handle) override {
        auto it = processes.find(handle.pid);
        if (it != processes.end()) {
            it->second.running = false;
            it->second.terminated = true;
            it->second.exit_code = 1;
            ++terminations;
        }
    }

    void release(RunningProcessHandle& handle) override {
        ++releases;
        handle.process = nullptr;
        handle.job = nullptr;
        handle.pid = 0;
    }

    void finish(DWORD exit_code) {
        for (auto& [pid, process] : processes) {
            if (process.running) {
                process.running = false;
                process.exit_code = exit_code;
            }
        }
    }

    std::map<DWORD, Process> processes;
    int launches = 0;
    int releases = 0;
    int terminations = 0;
    bool fail_launch = false;
    ProcessLaunchSpec last_spec;
};

// Временная БД в ./temp/scheduler/: создаётся и удаляется тестом.
// Путь приводится к абсолютному: Database::open требует локальный абсолютный путь.
class TestDatabase {
public:
    TestDatabase() {
        CreateDirectoryW(L"temp", nullptr);
        CreateDirectoryW(L"temp\\scheduler", nullptr);
        CreateDirectoryW(L"temp\\scheduler\\logs", nullptr);
        remove_files();
        opened_ = db_.open(database_path());
    }

    ~TestDatabase() {
        db_.close();
        remove_files();
    }

    Database& db() { return db_; }
    bool opened() const { return opened_; }
    std::wstring last_error() const { return db_.last_error(); }

    static std::wstring database_path() {
        wchar_t cwd[MAX_PATH] = {};
        GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstring base = cwd;
        for (auto& c : base) {
            if (c == L'\\') c = L'/';
        }
        return base + L"/temp/scheduler/scheduler-tests.db";
    }

private:
    static void remove_files() {
        const std::wstring path = database_path();
        DeleteFileW(path.c_str());
        DeleteFileW((path + L"-wal").c_str());
        DeleteFileW((path + L"-shm").c_str());
    }

    Database db_;
    bool opened_ = false;
};

TaskDefinition make_task(const wchar_t* id) {
    TaskDefinition task;
    task.id = id;
    task.name = L"Тестовая задача";
    task.enabled = true;
    task.program_path = L"d:/scripts/run.cmd";
    task.window_style = WindowStyle::Hidden;
    task.owner_sid = L"S-1-5-18";
    task.max_history_records = 100;
    return task;
}

void insert_daily_window(Database& db, const std::wstring& task_id, const wchar_t* window_id,
    const wchar_t* time) {
    TimeWindow window;
    window.id = window_id;
    window.task_id = task_id;
    window.kind = TriggerKind::Daily;
    window.exact_times = { time };
    db.insert_time_window(window);
}

void test_state_store_memory() {
    std::printf("trigger_state в памяти: идемпотентность\n");
    MemoryTriggerStateStore store;
    CHECK(!store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
    CHECK(store.try_mark_fired(L"t", L"w", L"2026-11-01T09:00", L"2026-11-01T06:00:00Z"));
    CHECK(store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
    CHECK(!store.try_mark_fired(L"t", L"w", L"2026-11-01T09:00", L"2026-11-01T06:01:00Z"));
    store.forget_task(L"t");
    CHECK(!store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
}

void test_state_store_database() {
    std::printf("trigger_state в БД: слот и last_fired_at\n");
    TestDatabase database;
    CHECK(database.opened());

    DatabaseTriggerStateStore store(database.db());
    CHECK(!store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
    CHECK(store.try_mark_fired(L"t", L"w", L"2026-11-01T09:00", L"2026-11-01T06:00:00Z"));
    CHECK(store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
    CHECK(!store.try_mark_fired(L"t", L"w", L"2026-11-01T09:00", L"2026-11-01T06:01:00Z"));
    store.forget_task(L"t");
    CHECK(!store.was_fired(L"t", L"w", L"2026-11-01T09:00"));
}

void test_path_validation() {
    std::printf("Валидация путей (SECURITY.md)\n");
    std::wstring error;
    CHECK(process::validate_local_path(L"d:/scripts/run.cmd", false, error));
    CHECK(process::validate_local_path(L"d:\\scripts\\run.cmd", false, error));
    CHECK(process::validate_local_path(L"", true, error));
    CHECK(!process::validate_local_path(L"", false, error));
    CHECK(!process::validate_local_path(L"scripts/run.cmd", false, error));
    CHECK(!process::validate_local_path(L"//server/share/run.cmd", false, error));
    CHECK(!process::validate_local_path(L"//?/d:/run.cmd", false, error));
    CHECK(!process::validate_local_path(L"d:/scripts/../run.cmd", false, error));
    CHECK(!process::validate_local_path(L"d:/scripts/run.cmd:stream", false, error));
    CHECK(!process::validate_local_path(L"d://scripts/run.cmd", false, error));
    CHECK(!process::validate_local_path(L"d:/", false, error));
}

void test_schedule_cache() {
    std::printf("Кэш расписания и инвалидация\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");

    ScheduleCache cache(database.db());
    auto first = cache.get();
    CHECK(first->size() == 1);
    CHECK(!first->empty() && first->front().windows.size() == 1);

    const int rebuilds = cache.rebuild_count();
    cache.get();
    CHECK(cache.rebuild_count() == rebuilds);

    cache.invalidate();
    cache.get();
    CHECK(cache.rebuild_count() == rebuilds + 1);
}

void test_scheduler_task_changed_invalidates_cache() {
    std::printf("Новая задача после старта: on_task_changed обновляет кэш\n");
    TestDatabase database;
    CHECK(database.opened());

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    auto clock = std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0));

    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(), std::move(clock));

    // Прогрев кэша без задач.
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 0);

    auto task = make_task(L"22222222-2222-2222-2222-222222222222");
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"bbbbbbbb-2222-2222-2222-222222222222", L"09:00");

    // Без уведомления кэш ещё пуст — задача не должна запуститься.
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 0);

    // После CRUD/импорта IPC обязан вызвать on_task_changed.
    scheduler.on_task_changed(task.id);
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);
}

void test_scheduler_idempotent_slot() {
    std::printf("Тик: один запуск на слот в минуту\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    auto clock = std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0));
    auto* clock_ptr = clock.get();

    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(), std::move(clock));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);
    scheduler.tick_once();
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);

    auto history = database.db().get_history(task.id, 10);
    CHECK(history.size() == 1);
    CHECK(!history.empty() && history[0].status == RunStatus::Running);

    runner_ptr->finish(0);
    scheduler.tick_once();
    history = database.db().get_history(task.id, 10);
    CHECK(!history.empty() && history[0].status == RunStatus::Completed);

    // Следующий день — новый слот.
    clock_ptr->advance(24 * 60);
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);
}

void test_scheduler_overlap_wait_complete() {
    std::printf("Наложение: WaitComplete и коалесинг\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.on_overlap = OnOverlapAction::WaitComplete;
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");
    insert_daily_window(database.db(), task.id, L"bbbbbbbb-1111-1111-1111-111111111111", L"09:00");

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(),
        std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0)));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1); // второй слот ждёт завершения

    runner_ptr->finish(0);
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);
}

void test_scheduler_overlap_start_another() {
    std::printf("Наложение: StartAnother\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.on_overlap = OnOverlapAction::StartAnother;
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");
    insert_daily_window(database.db(), task.id, L"bbbbbbbb-1111-1111-1111-111111111111", L"09:00");

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(),
        std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0)));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);
    CHECK(scheduler.running_task_count() == 1); // одна задача, два процесса
}

void test_scheduler_timeout() {
    std::printf("Таймаут и kill дерева процессов\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.timeout_minutes = 1;
    task.kill_on_timeout = true;
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    auto clock = std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0));
    auto* clock_ptr = clock.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(), std::move(clock));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);

    clock_ptr->advance(2);
    scheduler.tick_once();
    CHECK(runner_ptr->terminations == 1);

    const auto history = database.db().get_history(task.id, 10);
    CHECK(!history.empty() && history[0].status == RunStatus::Timeout);
    CHECK(scheduler.running_task_count() == 0);
}

void test_scheduler_retry() {
    std::printf("Повтор при ошибке: backoff и лимит попыток\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.on_error = OnErrorAction::Retry;
    task.retry_count = 1;
    database.db().insert_task(task);
    insert_daily_window(database.db(), task.id, L"aaaaaaaa-1111-1111-1111-111111111111", L"09:00");

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    auto clock = std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0));
    auto* clock_ptr = clock.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(), std::move(clock));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);

    runner_ptr->finish(3);
    scheduler.tick_once();

    // Пауза перед повтором — 1 минута.
    clock_ptr->advance(2);
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);

    // Лимит попыток исчерпан: третьего запуска нет.
    runner_ptr->finish(3);
    clock_ptr->advance(2);
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);
}

void test_scheduler_run_now_overlap() {
    std::printf("RunNow: единый путь и политика наложения\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.on_overlap = OnOverlapAction::WaitNext;
    database.db().insert_task(task);

    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::make_unique<FakeProcessRunner>(), std::make_unique<MemoryTriggerStateStore>(),
        std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0)));

    std::wstring error;
    const auto first = scheduler.run_now(task.id, error);
    CHECK(!first.empty());
    const auto second = scheduler.run_now(task.id, error);
    CHECK(second.empty()); // WaitNext: второй запуск отклонён
    CHECK(!error.empty());

    const auto history = database.db().get_history(task.id, 10);
    CHECK(history.size() == 1);
    CHECK(!history.empty() && history[0].status == RunStatus::Running);
}

void test_scheduler_recovery() {
    std::printf("Recovery: осиротевшие Running → Aborted\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    database.db().insert_task(task);

    RunHistory orphan;
    orphan.id = L"cccccccc-1111-1111-1111-111111111111";
    orphan.task_id = task.id;
    orphan.start_time = L"2026-11-01T06:00:00Z";
    orphan.pid = 42;
    orphan.status = RunStatus::Running;
    database.db().insert_history(orphan);

    {
        Scheduler scheduler(database.db(), L"temp/scheduler/logs",
            std::make_unique<FakeProcessRunner>(), std::make_unique<MemoryTriggerStateStore>(),
            std::make_unique<TestClock>(dt(2026, 11, 1, 10, 0)));
        scheduler.start();
        scheduler.stop();
    }

    const auto history = database.db().get_history(task.id, 10);
    CHECK(!history.empty() && history[0].status == RunStatus::Aborted);
    CHECK(!history.empty() && history[0].end_time.has_value());
}

void test_scheduler_interval_no_storm() {
    std::printf("Interval: один запуск на слот, без шторма\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.on_overlap = OnOverlapAction::StartAnother;
    database.db().insert_task(task);

    TimeWindow window;
    window.id = L"aaaaaaaa-1111-1111-1111-111111111111";
    window.task_id = task.id;
    window.kind = TriggerKind::Interval;
    window.type = TimeWindowType::Interval;
    window.start_time = L"09:00";
    window.end_time = L"10:00";
    window.repeat_interval_minutes = 20;
    database.db().insert_time_window(window);

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    auto clock = std::make_unique<TestClock>(dt(2026, 11, 1, 9, 20));
    auto* clock_ptr = clock.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(), std::move(clock));

    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);
    scheduler.tick_once();
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1); // та же минута — повторного запуска нет

    clock_ptr->advance(1); // 09:21 — не слот
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 1);

    clock_ptr->advance(19); // 09:40 — следующий слот
    scheduler.tick_once();
    CHECK(runner_ptr->launches == 2);
}

void test_scheduler_active_user_rejected() {
    std::printf("session_mode=ActiveUser в v1 отклоняется\n");
    TestDatabase database;
    CHECK(database.opened());

    auto task = make_task(L"11111111-1111-1111-1111-111111111111");
    task.session_mode = SessionMode::ActiveUser;
    database.db().insert_task(task);

    auto runner = std::make_unique<FakeProcessRunner>();
    auto* runner_ptr = runner.get();
    Scheduler scheduler(database.db(), L"temp/scheduler/logs",
        std::move(runner), std::make_unique<MemoryTriggerStateStore>(),
        std::make_unique<TestClock>(dt(2026, 11, 1, 9, 0)));

    std::wstring error;
    CHECK(scheduler.run_now(task.id, error).empty());
    CHECK(runner_ptr->launches == 0);
}

} // namespace

int main() {
    // Без буферизации: при падении видно, какой тест не завершился.
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Тесты планировщика WinSheduler\n\n");

    test_calendar();
    test_parsing();
    test_validation();
    test_mapping();
    test_daily();
    test_weekly();
    test_monthly();
    test_once_and_range();
    test_interval();
    test_catch_up();
    test_benchmark_1000_tasks();

    test_state_store_memory();
    test_state_store_database();
    test_path_validation();
    test_schedule_cache();
    test_scheduler_task_changed_invalidates_cache();
    test_scheduler_idempotent_slot();
    test_scheduler_overlap_wait_complete();
    test_scheduler_overlap_start_another();
    test_scheduler_timeout();
    test_scheduler_retry();
    test_scheduler_run_now_overlap();
    test_scheduler_recovery();
    test_scheduler_interval_no_storm();
    test_scheduler_active_user_rejected();

    std::printf("\nПройдено: %d, ошибок: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
