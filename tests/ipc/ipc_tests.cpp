// Контрактные тесты трека B: кадрирование IPC, конверты запросов, политика
// доступа, валидация входных данных и детерминированный fuzz JSON.
// Запуск: ./tests/ipc/run.ps1 (собирает этот файл вместе с модулями службы).

#include "json.h"
#include "ipc_frame.h"
#include "auth_policy.h"
#include "auth_validate.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

// ---------------------------------------------------------------------------
// Кадрирование v2
// ---------------------------------------------------------------------------

void test_framing() {
    const std::string payload = "{\"a\":1}";
    const std::string frame = ipc_frame::encode(payload);
    CHECK(frame.size() == ipc_frame::HEADER_BYTES + payload.size());
    // Длина little-endian.
    CHECK(static_cast<unsigned char>(frame[0]) == payload.size());
    CHECK(static_cast<unsigned char>(frame[1]) == 0);
    CHECK(frame.substr(ipc_frame::HEADER_BYTES) == payload);

    std::uint32_t len = 0;
    CHECK(ipc_frame::decode_header(reinterpret_cast<const unsigned char*>(frame.data()),
                                   frame.size(), len));
    CHECK(len == payload.size());

    // Пустое тело — длина 0.
    const std::string empty = ipc_frame::encode("");
    CHECK(empty.size() == ipc_frame::HEADER_BYTES);
    CHECK(empty[0] == 0 && empty[1] == 0 && empty[2] == 0 && empty[3] == 0);

    // Классификация префикса.
    std::uint32_t frame_len = 0;
    CHECK(ipc_frame::classify_prefix(reinterpret_cast<const unsigned char*>(frame.data()),
                                     frame.size(), frame_len) == ipc_frame::Prefix::Frame);
    CHECK(frame_len == payload.size());

    const std::string legacy = "{\"Action\":\"Ping\"}";
    CHECK(ipc_frame::classify_prefix(reinterpret_cast<const unsigned char*>(legacy.data()),
                                     legacy.size(), frame_len) == ipc_frame::Prefix::Legacy);

    const unsigned char short_prefix[2] = { 1, 0 };
    CHECK(ipc_frame::classify_prefix(short_prefix, 2, frame_len) == ipc_frame::Prefix::NeedMore);

    // Префикс больше лимита кадра — это не кадр.
    unsigned char too_big[4] = { 0, 0, 0x20, 0x00 }; // 2 МиБ
    CHECK(ipc_frame::classify_prefix(too_big, 4, frame_len) == ipc_frame::Prefix::Legacy);

    // Максимальный кадр ровно 1 МиБ допускается.
    const std::string max_payload(ipc_frame::MAX_FRAME_BYTES, 'a');
    const std::string max_frame = ipc_frame::encode(max_payload);
    CHECK(ipc_frame::classify_prefix(reinterpret_cast<const unsigned char*>(max_frame.data()),
                                     max_frame.size(), frame_len) == ipc_frame::Prefix::Frame);
    CHECK(frame_len == ipc_frame::MAX_FRAME_BYTES);
}

// ---------------------------------------------------------------------------
// JSON: лимиты и устойчивость
// ---------------------------------------------------------------------------

void test_json_limits() {
    using json::ParseStatus;

    CHECK(json::parse_ex("{\"a\":1}").status == ParseStatus::Ok);
    CHECK(json::parse_ex("").status == ParseStatus::Empty);
    CHECK(json::parse_ex("{").status == ParseStatus::Syntax);
    CHECK(json::parse_ex("{\"a\":1} trailing").status == ParseStatus::Syntax);
    CHECK(json::parse_ex("nonsense").status == ParseStatus::Syntax);

    // Глубина ровно 64 допустима, 65 — нет.
    const std::string depth64 = std::string(64, '[') + std::string(64, ']');
    const std::string depth65 = std::string(65, '[') + std::string(65, ']');
    CHECK(json::parse_ex(depth64).status == ParseStatus::Ok);
    CHECK(json::parse_ex(depth65).status == ParseStatus::TooDeep);

    // Строка 64 КиБ допустима, больше — нет.
    const std::string ok_string = "\"" + std::string(json::Limits{}.max_string_bytes, 'a') + "\"";
    CHECK(json::parse_ex(ok_string).status == ParseStatus::Ok);
    const std::string long_string = "\"" + std::string(json::Limits{}.max_string_bytes + 1, 'a') + "\"";
    CHECK(json::parse_ex(long_string).status == ParseStatus::StringTooLong);

    // Документ больше 1 МиБ отклоняется целиком.
    const std::string huge(json::Limits{}.max_bytes + 1, ' ');
    CHECK(json::parse_ex(huge).status == ParseStatus::TooLarge);

    // Кириллица в UTF-8 переживает round-trip.
    const std::string utf8 = "{\"name\":\"Ночной бэкап\"}";
    auto parsed = json::parse_ex(utf8);
    CHECK(parsed.ok());
    CHECK(parsed.value.get("name") != nullptr);
    CHECK(json::serialize(parsed.value).find("name") != std::string::npos);
}

// ---------------------------------------------------------------------------
// UTF-8: битые последовательности и суррогатные пары (замечания трека G)
// ---------------------------------------------------------------------------

void test_json_utf8() {
    // Битый lead-байт заменяется на '?', следующий символ не теряется.
    CHECK(json::escape("\xCD\x60") == "?`");
    CHECK(json::escape("\xE0\x41") == "?A");

    // Суррогатная пара в \u-escape — один codepoint UTF-8 (4 байта).
    CHECK(json::unescape("\\ud83d\\ude00") == "\xF0\x9F\x98\x80");
    CHECK(json::escape("\xF0\x9F\x98\x80") == "\\ud83d\\ude00");

    // Кириллица в \u-escape: U+041D U+0435 U+0442 (сравнение по байтам UTF-8).
    CHECK(json::unescape("\\u041d\\u0435\\u0442") == "\xD0\x9D\xD0\xB5\xD1\x82");

    // Тот же разбор внутри строки JSON.
    auto parsed = json::parse_ex("\"\\ud83d\\ude00\"");
    CHECK(parsed.ok());
    CHECK(parsed.value.is_string());
    CHECK(parsed.value.as_string() == "\xF0\x9F\x98\x80");

    // Одиночный суррогат не превращается в мусорную пару.
    auto lone = json::parse_ex("\"\\ud83d\"");
    CHECK(lone.ok());
    CHECK(lone.value.as_string().size() == 3); // U+D83D в UTF-8
}

// ---------------------------------------------------------------------------
// Конверты запросов: v2 и переходный legacy
// ---------------------------------------------------------------------------

void test_envelope() {
    using ipc_frame::EnvelopeStatus;

    {
        ipc_frame::Envelope env;
        const std::string body =
            "{\"v\":1,\"id\":\"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60\","
            "\"action\":\"Ping\",\"payload\":{}}";
        CHECK(ipc_frame::parse_envelope(body, env) == EnvelopeStatus::Ok);
        CHECK(!env.legacy);
        CHECK(env.action == "Ping");
        CHECK(env.id == "8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60");
        CHECK(env.payload == "{}");
    }

    {
        // Неизвестная мажорная версия.
        ipc_frame::Envelope env;
        CHECK(ipc_frame::parse_envelope("{\"v\":2,\"action\":\"Ping\"}", env) ==
              EnvelopeStatus::UnsupportedVersion);
    }

    {
        // Legacy-конверт без версии.
        ipc_frame::Envelope env;
        CHECK(ipc_frame::parse_envelope("{\"Action\":\"ListTasks\",\"Payload\":\"\"}", env) ==
              EnvelopeStatus::Ok);
        CHECK(env.legacy);
        CHECK(env.action == "ListTasks");
        CHECK(env.id.empty());
    }

    {
        // Legacy Payload — строка с вложенным JSON.
        ipc_frame::Envelope env;
        const std::string body =
            "{\"Action\":\"CreateTask\",\"Payload\":\"{\\\"Name\\\":\\\"x\\\"}\"}";
        CHECK(ipc_frame::parse_envelope(body, env) == EnvelopeStatus::Ok);
        CHECK(env.legacy);
        CHECK(env.payload == "{\"Name\":\"x\"}");
    }

    {
        // Legacy Payload — идентификатор задачи строкой.
        ipc_frame::Envelope env;
        const std::string body =
            "{\"Action\":\"RunNow\",\"Payload\":\"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60\"}";
        CHECK(ipc_frame::parse_envelope(body, env) == EnvelopeStatus::Ok);
        CHECK(env.legacy);
        CHECK(env.payload == "\"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60\"");
    }

    {
        ipc_frame::Envelope env;
        CHECK(ipc_frame::parse_envelope("not json", env) == EnvelopeStatus::Invalid);
        CHECK(ipc_frame::parse_envelope("[]", env) == EnvelopeStatus::Invalid);
        CHECK(ipc_frame::parse_envelope("{\"v\":1}", env) == EnvelopeStatus::Invalid);
        CHECK(ipc_frame::parse_envelope("{\"Action\":\"\"}", env) == EnvelopeStatus::Invalid);
    }

    {
        // Превышение глубины даёт TOO_LARGE, а не VALIDATION.
        ipc_frame::Envelope env;
        const std::string deep = "{\"Action\":\"Ping\",\"Payload\":" +
                                 std::string(70, '[') + std::string(70, ']') + "}";
        CHECK(ipc_frame::parse_envelope(deep, env) == EnvelopeStatus::TooLarge);
    }
}

// ---------------------------------------------------------------------------
// Политика доступа
// ---------------------------------------------------------------------------

void test_auth_policy() {
    using auth::Action;
    using auth::Identity;
    using auth::Policy;

    Identity guest;
    guest.sid = L"S-1-5-21-1-2-3-1001";
    guest.authenticated = true;

    Identity op = guest;
    op.is_operator = true;

    Identity admin = guest;
    admin.is_admin = true;

    // Гость: только Ping.
    CHECK(Policy::allows(guest, Action::Ping));
    CHECK(!Policy::allows(guest, Action::GetStatus));
    CHECK(!Policy::allows(guest, Action::ListTasks));
    CHECK(!Policy::allows(guest, Action::RunNow));

    // Неаутентифицированный клиент не получает даже Ping-зависимых действий.
    Identity anonymous;
    CHECK(Policy::allows(anonymous, Action::Ping));
    CHECK(!Policy::allows(anonymous, Action::GetStatus));

    // Оператор: свои задачи, без привилегированных действий.
    CHECK(Policy::allows(op, Action::GetStatus));
    CHECK(Policy::allows(op, Action::GetDashboard));
    CHECK(Policy::allows(op, Action::ListTasks));
    CHECK(Policy::allows(op, Action::GetTask));
    CHECK(Policy::allows(op, Action::CreateTask));
    CHECK(Policy::allows(op, Action::UpdateTask));
    CHECK(Policy::allows(op, Action::DeleteTask));
    CHECK(Policy::allows(op, Action::SetTaskEnabled));
    CHECK(Policy::allows(op, Action::RunNow));
    CHECK(!Policy::allows(op, Action::GetAudit));
    CHECK(!Policy::allows(op, Action::GetSettings));
    CHECK(!Policy::allows(op, Action::SetSettings));

    // Администратор: всё.
    CHECK(Policy::allows(admin, Action::GetAudit));
    CHECK(Policy::allows(admin, Action::GetSettings));
    CHECK(Policy::allows(admin, Action::SetSettings));
    CHECK(Policy::allows(admin, Action::RunNow));
    CHECK(!Policy::allows(admin, Action::Unknown));

    // Привилегированные поля.
    CHECK(!Policy::can_use_credentials(op));
    CHECK(Policy::can_use_credentials(admin));
    CHECK(!Policy::can_run_as_system(op));
    CHECK(Policy::can_run_as_system(admin));
    CHECK(!Policy::can_use_active_user(op));
    CHECK(Policy::can_use_active_user(admin));

    // Фильтрация по владельцу.
    CHECK(Policy::can_access_task(op, L"S-1-5-21-1-2-3-1001"));
    CHECK(Policy::can_access_task(op, L"s-1-5-21-1-2-3-1001")); // регистр не важен
    CHECK(!Policy::can_access_task(op, L"S-1-5-21-1-2-3-1002"));
    CHECK(!Policy::can_access_task(op, L""));
    CHECK(!Policy::can_access_task(op, L"S-1-5-18")); // SYSTEM
    CHECK(Policy::can_access_task(admin, L"S-1-5-21-1-2-3-1002"));
    CHECK(Policy::can_access_task(admin, L""));
    CHECK(!Policy::can_access_task(guest, L"S-1-5-21-1-2-3-1001"));

    // Имена действий.
    CHECK(auth::action_from_name(L"GetStatus") == Action::GetStatus);
    CHECK(auth::action_from_name(L"SetTaskEnabled") == Action::SetTaskEnabled);
    CHECK(auth::action_from_name(L"EnableTask") == Action::SetTaskEnabled);
    CHECK(auth::action_from_name(L"DisableTask") == Action::SetTaskEnabled);
    CHECK(auth::action_from_name(L"Bogus") == Action::Unknown);
}

// ---------------------------------------------------------------------------
// Валидация входных данных
// ---------------------------------------------------------------------------

void test_validation() {
    // GUID.
    CHECK(auth::is_guid(L"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60"));
    CHECK(auth::is_guid(L"8F1C2C1E-4A0B-4C1E-9A6E-1F2D3C4B5A60"));
    CHECK(!auth::is_guid(L""));
    CHECK(!auth::is_guid(L"8f1c2c1e4a0b4c1e9a6e1f2d3c4b5a60"));
    CHECK(!auth::is_guid(L"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a6"));
    CHECK(!auth::is_guid(L"zzzzzzzz-4a0b-4c1e-9a6e-1f2d3c4b5a60"));

    // Пути.
    CHECK(auth::is_local_absolute_path(L"d:/scripts/backup.cmd"));
    CHECK(auth::is_local_absolute_path(L"c:\\scripts\\backup.cmd"));
    CHECK(!auth::is_local_absolute_path(L""));
    CHECK(!auth::is_local_absolute_path(L"relative/backup.cmd"));
    CHECK(!auth::is_local_absolute_path(L"//server/share/a.cmd"));   // UNC
    CHECK(!auth::is_local_absolute_path(L"\\\\?\\c:/a.cmd"));        // device path
    CHECK(!auth::is_local_absolute_path(L"c:/a/../b.cmd"));          // ..
    CHECK(!auth::is_local_absolute_path(L"c:/a//b.cmd"));            // пустой сегмент
    CHECK(!auth::is_local_absolute_path(L"c:a.txt"));                // относительно диска
    CHECK(!auth::is_local_absolute_path(L"c:/a/b.txt:hidden"));      // альтернативный поток
    CHECK(!auth::is_local_absolute_path(L"c:/a/b\n.cmd"));           // перевод строки

    CHECK(auth::is_valid_working_directory(L""));
    CHECK(auth::is_valid_working_directory(L"c:/scripts"));
    CHECK(!auth::is_valid_working_directory(L"c:/"));  // корень диска
    CHECK(!auth::is_valid_working_directory(L"//server/share"));

    // Время и даты.
    CHECK(auth::is_hhmm(L"02:00"));
    CHECK(auth::is_hhmm(L"23:59"));
    CHECK(!auth::is_hhmm(L"24:00"));
    CHECK(!auth::is_hhmm(L"2:00"));
    CHECK(!auth::is_hhmm(L"02:60"));
    CHECK(auth::is_yyyy_mm_dd(L"2026-11-01"));
    CHECK(!auth::is_yyyy_mm_dd(L"2026-13-01"));
    CHECK(!auth::is_yyyy_mm_dd(L"2026-11-32"));
    CHECK(auth::is_yyyy_mm_dd_thh_mm(L"2026-11-01T09:30"));
    CHECK(!auth::is_yyyy_mm_dd_thh_mm(L"2026-11-01 09:30"));

    // Задача.
    TaskDefinition task;
    task.name = L"Бэкап";
    task.program_path = L"d:/scripts/backup.cmd";
    std::wstring error;
    CHECK(auth::validate_task(task, false, error));

    TaskDefinition active_user = task;
    active_user.session_mode = SessionMode::ActiveUser;
    CHECK(!auth::validate_task(active_user, true, error));

    TaskDefinition bad_retry = task;
    bad_retry.retry_count = 11;
    CHECK(!auth::validate_task(bad_retry, true, error));

    TaskDefinition bad_history = task;
    bad_history.max_history_records = 0;
    CHECK(!auth::validate_task(bad_history, true, error));

    TaskDefinition no_name = task;
    no_name.name.clear();
    CHECK(!auth::validate_task(no_name, true, error));

    TaskDefinition run_as = task;
    RunAsCredentials credentials;
    credentials.user_name = L"svc";
    run_as.run_as_user = credentials;
    CHECK(!auth::validate_task(run_as, false, error)); // не-админ не может RunAs
    CHECK(auth::validate_task(run_as, true, error));

    // Триггеры.
    TimeWindow daily;
    daily.kind = TriggerKind::Daily;
    daily.exact_times = { L"02:00" };
    CHECK(auth::validate_time_window(daily, error));

    TimeWindow daily_empty = daily;
    daily_empty.exact_times.clear();
    CHECK(!auth::validate_time_window(daily_empty, error));

    TimeWindow weekly = daily;
    weekly.kind = TriggerKind::Weekly;
    weekly.days_of_week = { 1, 3, 5 };
    CHECK(auth::validate_time_window(weekly, error));

    TimeWindow interval;
    interval.kind = TriggerKind::Interval;
    interval.start_time = L"09:00";
    interval.end_time = L"18:00";
    interval.repeat_interval_minutes = 15;
    CHECK(auth::validate_time_window(interval, error));

    TimeWindow interval_bad = interval;
    interval_bad.start_time = L"18:00";
    CHECK(!auth::validate_time_window(interval_bad, error));

    TimeWindow interval_no_repeat = interval;
    interval_no_repeat.repeat_interval_minutes.reset();
    CHECK(!auth::validate_time_window(interval_no_repeat, error));

    TimeWindow monthly = daily;
    monthly.kind = TriggerKind::Monthly;
    monthly.days_of_month = { 1, 15 };
    CHECK(auth::validate_time_window(monthly, error));

    // Защита от log injection.
    CHECK(auth::sanitize_line(L"a\r\nb\tc") == L"a  b c");
}

// ---------------------------------------------------------------------------
// Детерминированный fuzz: разбор не должен падать и бросать исключения
// ---------------------------------------------------------------------------

void test_fuzz() {
    const std::string seed =
        "{\"v\":1,\"id\":\"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60\",\"action\":\"CreateTask\","
        "\"payload\":{\"name\":\"Ночной бэкап\",\"timeWindows\":[{\"kind\":\"Daily\","
        "\"exactTimes\":[\"02:00\"]}],\"retryCount\":3}}";

    std::uint32_t state = 0x12345678u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    int parsed_ok = 0;
    for (int iteration = 0; iteration < 5000; ++iteration) {
        std::string mutated = seed;
        const int mutations = 1 + static_cast<int>(next() % 8);
        for (int m = 0; m < mutations && !mutated.empty(); ++m) {
            const std::size_t position = next() % mutated.size();
            switch (next() % 3) {
            case 0: mutated[position] = static_cast<char>(next() & 0xFF); break;
            case 1: mutated.erase(position, 1); break;
            default: mutated.insert(position, 1, static_cast<char>(next() & 0xFF)); break;
            }
        }

        auto result = json::parse_ex(mutated);
        if (result.ok()) {
            ++parsed_ok;
            // Сериализация разобранного значения тоже не должна падать.
            (void)json::serialize(result.value);
        }

        ipc_frame::Envelope envelope;
        (void)ipc_frame::parse_envelope(mutated, envelope);
    }

    // Часть мутаций обязана остаться валидной — иначе тест ничего не проверяет.
    CHECK(parsed_ok > 0);

    // Случайные бинарные данные любой длины.
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const std::size_t length = next() % 512;
        std::string random_bytes(length, '\0');
        for (std::size_t i = 0; i < length; ++i) {
            random_bytes[i] = static_cast<char>(next() & 0xFF);
        }
        (void)json::parse_ex(random_bytes);
        ipc_frame::Envelope envelope;
        (void)ipc_frame::parse_envelope(random_bytes, envelope);
    }
}

} // namespace

int main() {
    test_framing();
    test_json_limits();
    test_json_utf8();
    test_envelope();
    test_auth_policy();
    test_validation();
    test_fuzz();

    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("IPC contract tests FAILED\n");
        return 1;
    }
    std::printf("IPC contract tests passed\n");
    return 0;
}
