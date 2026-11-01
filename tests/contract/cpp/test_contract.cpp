// Контрактные тесты на общих фикстурах ./tests/contract/fixtures.
//
// Проверяют инварианты конверта IPC (./docs/contracts/IPC.md) и формы DTO.
// JSON-схемы (draft 2020-12) проверяются на стороне C#: здесь — структурные
// инварианты, которые обязаны выполняться и без схем.

#include "test_harness.h"

#include "json.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxFrameBytes = 1024 * 1024; // IPC.md, п. 1: 1 МиБ.
constexpr size_t kMaxStringBytes = 64 * 1024;  // IPC.md, п. 1: 64 КиБ.

std::string read_fixture(const std::string& name) {
    const std::string path = test::fixtures_dir() + "/" + name;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        test::report(__FILE__, __LINE__, "не найдена фикстура: " + path);
        return {};
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool is_guid(const std::string& value) {
    if (value.size() != 36) {
        return false;
    }

    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return false;
            }
            continue;
        }
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

bool is_utc_timestamp(const std::string& value) {
    // Достаточно формы yyyy-MM-ddTHH:mm:ssZ из IPC.md, п. 5.
    return value.size() == 20 && value[4] == '-' && value[7] == '-' &&
        value[10] == 'T' && value[13] == ':' && value[16] == ':' && value[19] == 'Z';
}

bool in_list(const std::string& value, const std::vector<std::string>& allowed) {
    for (const auto& item : allowed) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

// Рекурсивно проверяет, что в документе нет строк длиннее лимита контракта.
void check_string_lengths(const json::Value& value, const std::string& path) {
    if (value.is_string()) {
        CHECK_MSG(value.as_string().size() <= kMaxStringBytes,
            ("строка длиннее 64 КиБ: " + path).c_str());
        return;
    }
    if (value.is_array()) {
        for (size_t i = 0; i < value.as_array().size(); ++i) {
            check_string_lengths(value.as_array()[i], path + "[" + std::to_string(i) + "]");
        }
        return;
    }
    if (value.is_object()) {
        for (const auto& [key, item] : value.as_object()) {
            check_string_lengths(item, path + "." + key);
        }
    }
}

const json::Value* field(const json::Value& object, const std::string& key, const char* type_name) {
    if (!object.is_object()) {
        test::report(__FILE__, __LINE__, std::string("ожидался объект для поля ") + key);
        return nullptr;
    }

    const auto* value = object.get(key);
    if (value == nullptr) {
        test::report(__FILE__, __LINE__, "отсутствует поле: " + key);
        return nullptr;
    }

    const std::string expected = type_name;
    const bool matches =
        (expected == "string" && value->is_string()) ||
        (expected == "number" && value->is_number()) ||
        (expected == "bool" && value->is_bool()) ||
        (expected == "object" && value->is_object()) ||
        (expected == "array" && value->is_array()) ||
        (expected == "null" && value->is_null());

    if (!matches) {
        test::report(__FILE__, __LINE__, "поле " + key + " имеет неверный тип, ожидался " + expected);
        return nullptr;
    }

    return value;
}

std::string string_field(const json::Value& object, const std::string& key) {
    const auto* value = field(object, key, "string");
    return value != nullptr ? value->as_string() : std::string();
}

} // namespace

TEST_CASE(contract_fixtures_exist) {
    const std::vector<std::string> required = {
        "ipc-request.json",
        "ipc-response-ok.json",
        "ipc-response-error.json",
        "ipc-legacy-request.json",
        "ipc-legacy-response.json",
        "task-dto.json",
        "time-window-dto.json",
        "run-history-dto.json",
        "service-status.json",
        "manifest.json",
    };

    for (const auto& name : required) {
        const std::string content = read_fixture(name);
        CHECK_MSG(!content.empty(), name.c_str());
        if (!content.empty()) {
            CHECK(json::parse(content).is_object() || json::parse(content).is_array());
        }
    }
}

TEST_CASE(contract_request_envelope) {
    const auto document = json::parse(read_fixture("ipc-request.json"));
    REQUIRE(document.is_object());

    const auto* version = field(document, "v", "number");
    const auto* id = field(document, "id", "string");
    const auto* action = field(document, "action", "string");
    field(document, "payload", "object");

    if (version != nullptr) {
        CHECK_EQ(version->as_int(), 1);
    }
    if (id != nullptr) {
        CHECK_MSG(is_guid(id->as_string()), "id запроса должен быть GUID");
    }
    if (action != nullptr) {
        CHECK(!action->as_string().empty());
    }
}

TEST_CASE(contract_response_ok_envelope) {
    const auto document = json::parse(read_fixture("ipc-response-ok.json"));
    REQUIRE(document.is_object());

    const auto* version = field(document, "v", "number");
    const auto* id = field(document, "id", "string");
    const auto* ok = field(document, "ok", "bool");
    field(document, "error", "null");
    const auto* payload = field(document, "payload", "object");

    if (version != nullptr) {
        CHECK_EQ(version->as_int(), 1);
    }
    if (id != nullptr) {
        CHECK_MSG(is_guid(id->as_string()), "id ответа должен быть GUID");
    }
    if (ok != nullptr) {
        CHECK_EQ(ok->as_bool(), true);
    }
    if (payload != nullptr) {
        const auto* pong = field(*payload, "pong", "bool");
        if (pong != nullptr) {
            CHECK_EQ(pong->as_bool(), true);
        }
        string_field(*payload, "serviceVersion");
    }
}

TEST_CASE(contract_response_error_envelope) {
    const auto document = json::parse(read_fixture("ipc-response-error.json"));
    REQUIRE(document.is_object());

    const auto* ok = field(document, "ok", "bool");
    field(document, "payload", "null");
    const auto* error = field(document, "error", "object");

    if (ok != nullptr) {
        CHECK_EQ(ok->as_bool(), false);
    }

    REQUIRE(error != nullptr);
    const std::string code = string_field(*error, "code");
    const std::string message = string_field(*error, "message");

    CHECK_MSG(in_list(code, {
        "AUTH_DENIED", "NOT_FOUND", "VALIDATION", "CONFLICT",
        "TOO_LARGE", "UNSUPPORTED_VERSION", "DB", "INTERNAL" }),
        "код ошибки должен быть из IPC.md, п. 6");
    CHECK(!message.empty());
}

TEST_CASE(contract_legacy_envelope) {
    const auto request = json::parse(read_fixture("ipc-legacy-request.json"));
    REQUIRE(request.is_object());

    CHECK(request.get("v") == nullptr);
    string_field(request, "Action");
    string_field(request, "Payload");

    const auto response = json::parse(read_fixture("ipc-legacy-response.json"));
    REQUIRE(response.is_object());

    CHECK(response.get("v") == nullptr);
    field(response, "Success", "bool");
    string_field(response, "Error");
    string_field(response, "Payload");
}

TEST_CASE(contract_task_dto_shape) {
    const auto document = json::parse(read_fixture("task-dto.json"));
    REQUIRE(document.is_object());

    const std::string id = string_field(document, "id");
    CHECK_MSG(is_guid(id), "TaskDto.id должен быть GUID");

    const std::string name = string_field(document, "name");
    CHECK(!name.empty());

    field(document, "enabled", "bool");
    string_field(document, "programPath");
    field(document, "hasPassword", "bool");
    field(document, "timeWindows", "array");

    const std::string window_style = string_field(document, "windowStyle");
    CHECK_MSG(in_list(window_style, { "Normal", "Hidden", "Minimized", "Maximized" }),
        "windowStyle вне перечисления IPC.md");

    const std::string session_mode = string_field(document, "sessionMode");
    CHECK_MSG(in_list(session_mode, { "Headless", "ActiveUser" }),
        "sessionMode вне перечисления IPC.md");

    const std::string on_error = string_field(document, "onError");
    CHECK_MSG(in_list(on_error, { "SkipNext", "Retry", "Fail" }), "onError вне перечисления");

    const std::string on_overlap = string_field(document, "onOverlap");
    CHECK_MSG(in_list(on_overlap, { "WaitComplete", "WaitNext", "StartAnother" }),
        "onOverlap вне перечисления");

    const std::string created_at = string_field(document, "createdAt");
    CHECK_MSG(is_utc_timestamp(created_at), "createdAt должен быть UTC ISO 8601 с Z");

    const auto* windows = field(document, "timeWindows", "array");
    REQUIRE(windows != nullptr);
    CHECK(!windows->as_array().empty());

    for (const auto& window : windows->as_array()) {
        REQUIRE(window.is_object());
        const std::string kind = string_field(window, "kind");
        CHECK_MSG(in_list(kind, { "Once", "Daily", "Weekly", "Monthly", "Interval" }),
            "TimeWindowDto.kind вне перечисления");
        CHECK_MSG(is_guid(string_field(window, "id")), "TimeWindowDto.id должен быть GUID");
    }
}

TEST_CASE(contract_interval_window_rules) {
    const auto document = json::parse(read_fixture("time-window-dto.json"));
    REQUIRE(document.is_object());

    CHECK_EQ(string_field(document, "kind"), std::string("Interval"));

    const std::string start = string_field(document, "startTime");
    const std::string end = string_field(document, "endTime");
    CHECK_MSG(start.size() == 5 && start[2] == ':', "startTime должен быть HH:mm");
    CHECK_MSG(end.size() == 5 && end[2] == ':', "endTime должен быть HH:mm");
    CHECK_MSG(start < end, "для Interval требуется startTime < endTime");

    const auto* interval = field(document, "repeatIntervalMinutes", "number");
    REQUIRE(interval != nullptr);
    CHECK(interval->as_int() >= 1 && interval->as_int() <= 1440);
}

TEST_CASE(contract_run_history_shape) {
    const auto document = json::parse(read_fixture("run-history-dto.json"));
    REQUIRE(document.is_object());

    CHECK_MSG(is_guid(string_field(document, "id")), "RunHistoryDto.id должен быть GUID");
    CHECK_MSG(is_guid(string_field(document, "taskId")), "RunHistoryDto.taskId должен быть GUID");
    CHECK_MSG(is_utc_timestamp(string_field(document, "startTime")),
        "RunHistoryDto.startTime должен быть UTC ISO 8601 с Z");

    const std::string status = string_field(document, "status");
    CHECK_MSG(in_list(status, { "Running", "Completed", "Failed", "Killed", "Timeout", "Aborted" }),
        "RunHistoryDto.status вне перечисления");

    const std::string triggered_by = string_field(document, "triggeredBy");
    CHECK_MSG(in_list(triggered_by, { "Schedule", "Manual" }), "triggeredBy вне перечисления");
}

TEST_CASE(contract_service_status_shape) {
    const auto document = json::parse(read_fixture("service-status.json"));
    REQUIRE(document.is_object());

    field(document, "running", "bool");
    field(document, "isAdmin", "bool");
    field(document, "isOperator", "bool");
    field(document, "canManageSystemTasks", "bool");
    field(document, "dbReady", "bool");
    field(document, "taskCount", "number");
    field(document, "runningTaskCount", "number");
    string_field(document, "serviceVersion");
    CHECK_MSG(is_utc_timestamp(string_field(document, "startedAt")),
        "GetStatus.startedAt должен быть UTC ISO 8601 с Z");
}

TEST_CASE(contract_fixture_limits) {
    const std::vector<std::string> names = {
        "ipc-request.json",
        "ipc-response-ok.json",
        "ipc-response-error.json",
        "task-dto.json",
        "time-window-dto.json",
        "run-history-dto.json",
        "service-status.json",
    };

    for (const auto& name : names) {
        const std::string content = read_fixture(name);
        CHECK_MSG(content.size() <= kMaxFrameBytes, (name + ": кадр больше 1 МиБ").c_str());

        const auto document = json::parse(content);
        check_string_lengths(document, name);
    }
}

TEST_CASE(contract_serialization_roundtrip_of_fixtures) {
    const std::vector<std::string> names = {
        "ipc-request.json",
        "ipc-response-ok.json",
        "ipc-response-error.json",
        "task-dto.json",
        "time-window-dto.json",
        "run-history-dto.json",
        "service-status.json",
    };

    for (const auto& name : names) {
        const auto original = json::parse(read_fixture(name));
        REQUIRE(original.is_object());

        const std::string text = json::serialize(original);
        const auto restored = json::parse(text);

        // Сериализация и повторный разбор не теряют обязательные поля.
        for (const auto& [key, value] : original.as_object()) {
            const auto* copy = restored.get(key);
            CHECK_MSG(copy != nullptr, (name + ": потеряно поле " + key).c_str());
            if (copy == nullptr) {
                continue;
            }
            CHECK_MSG(copy->data.index() == value.data.index(),
                (name + ": изменился тип поля " + key).c_str());
        }
    }
}
