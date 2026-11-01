// Unit-тесты JSON-модуля службы (./src/WinShedulerSvc/json.cpp).
//
// Проверяют разбор, сериализацию, экранирование и устойчивость к мусору.
// Модуль компилируется в тестовый исполняемый файл напрямую, поэтому тесты
// не зависят от сборки службы целиком.

#include "test_harness.h"

#include "json.h"

#include <cstdint>
#include <string>

namespace {

// Структурное сравнение значений: порядок ключей в unordered_map не важен.
bool same(const json::Value& left, const json::Value& right) {
    if (left.data.index() != right.data.index()) {
        return false;
    }

    if (left.is_null()) {
        return true;
    }
    if (left.is_bool()) {
        return left.as_bool() == right.as_bool();
    }
    if (left.is_number()) {
        return left.as_number() == right.as_number();
    }
    if (left.is_string()) {
        return left.as_string() == right.as_string();
    }
    if (left.is_array()) {
        const auto& a = left.as_array();
        const auto& b = right.as_array();
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (!same(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }

    const auto& a = left.as_object();
    const auto& b = right.as_object();
    if (a.size() != b.size()) {
        return false;
    }
    for (const auto& [key, value] : a) {
        auto it = b.find(key);
        if (it == b.end() || !same(value, it->second)) {
            return false;
        }
    }
    return true;
}

// Детерминированный генератор: одинаковый набор данных в каждом прогоне.
struct XorShift32 {
    uint32_t state = 0x9E3779B9u;

    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

std::string repeat(char value, size_t count) {
    return std::string(count, value);
}

} // namespace

TEST_CASE(json_roundtrip_document) {
    const std::string source =
        R"({"v":1,"id":"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60","ok":true,)"
        R"("payload":{"name":"Ночной бэкап","count":12,"ratio":0.5,)"
        R"("nested":{"days":[1,2,3],"flag":false,"empty":null}}})";

    const auto first = json::parse(source);
    REQUIRE(first.is_object());

    const std::string text = json::serialize(first);
    const auto second = json::parse(text);

    CHECK(same(first, second));
    CHECK_EQ(text.find('\n'), std::string::npos);
}

TEST_CASE(json_unicode_preserved) {
    const std::string source = "{\"name\":\"Ночной бэкап — тест\"}";
    const auto value = json::parse(source);

    REQUIRE(value.is_object());
    const auto* name = value.get("name");
    REQUIRE(name != nullptr);
    CHECK(name->is_string());
    CHECK_EQ(name->as_string(), std::string("Ночной бэкап — тест"));
}

TEST_CASE(json_escape_unescape_roundtrip) {
    const std::string samples[] = {
        "plain ascii",
        "Ночной бэкап",
        "quote\" backslash\\ slash/",
        "tab\tnewline\nreturn\r",
        "control:\x01\x02\x1f",
    };

    for (const auto& sample : samples) {
        const std::string escaped = json::escape(sample);
        CHECK_EQ(json::unescape(escaped), sample);
    }
}

TEST_CASE(json_escape_produces_ascii) {
    const std::string escaped = json::escape("Ночной бэкап");
    for (unsigned char c : escaped) {
        CHECK(c < 0x80);
        if (c >= 0x80) {
            break;
        }
    }
    CHECK(escaped.find("\\u") != std::string::npos);
}

TEST_CASE(json_escape_control_characters) {
    const std::string text = "line1\nline2\ttab\"quote\"";
    const std::string escaped = json::escape(text);

    CHECK_EQ(escaped.find('\n'), std::string::npos);
    CHECK_EQ(escaped.find('\t'), std::string::npos);
    CHECK(escaped.find("\\n") != std::string::npos);
    CHECK(escaped.find("\\t") != std::string::npos);
}

TEST_CASE(json_numbers_and_literals) {
    const auto value = json::parse(R"({"a":-7,"b":0,"c":1234,"d":true,"e":false,"f":null})");
    REQUIRE(value.is_object());

    const auto* a = value.get("a");
    const auto* c = value.get("c");
    const auto* d = value.get("d");
    const auto* f = value.get("f");

    REQUIRE(a != nullptr && c != nullptr && d != nullptr && f != nullptr);
    CHECK_EQ(a->as_int(), -7);
    CHECK_EQ(c->as_int(), 1234);
    CHECK_EQ(d->as_bool(), true);
    CHECK(f->is_null());
}

TEST_CASE(json_empty_and_broken_input_does_not_crash) {
    const std::string inputs[] = {
        "",
        "{",
        "[",
        "\"unterminated",
        "{\"key\"}",
        "{\"key\":}",
        "[[[[[",
        "}{",
        "\x01\x02\x03",
        "null",
        "true",
        "0",
    };

    for (const auto& input : inputs) {
        const auto value = json::parse(input);
        const std::string text = json::serialize(value);
        CHECK(!text.empty());
    }
}

TEST_CASE(json_truncated_document_does_not_crash) {
    const std::string source = R"({"v":1,"payload":{"times":["02:00","03:00"],"n":5}})";

    for (size_t length = 0; length <= source.size(); ++length) {
        const auto value = json::parse(source.substr(0, length));
        const std::string text = json::serialize(value);
        CHECK(!text.empty());
    }
}

TEST_CASE(json_fuzz_random_bytes_does_not_crash) {
    XorShift32 random;

    for (int iteration = 0; iteration < 500; ++iteration) {
        const size_t length = random.next() % 256;
        std::string input;
        input.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            input.push_back(static_cast<char>(random.next() & 0xFF));
        }

        // Произвольные байты: главное — не падать и не зависать.
        // Идемпотентность сериализации здесь не проверяется: невалидный UTF-8
        // нормализуется, а escape() сейчас съедает байт после битого lead-байта
        // (дефект в зоне B, зафиксирован в ./docs/status/g-requests.md).
        const auto value = json::parse(input);
        const std::string text = json::serialize(value);

        CHECK(!text.empty());
        CHECK(!json::serialize(json::parse(text)).empty());
    }
}

TEST_CASE(json_serialization_idempotent_for_valid_utf8) {
    // Корректный UTF-8 (ASCII + кириллица + спецсимволы) должен переживать
    // повторную сериализацию без изменений.
    const std::string samples[] = {
        "plain",
        "Ночной бэкап",
        "Кириллица и латиница mixed",
        "Символы: !@#$%^&*()_+-=[]{};':\",./<>?",
        "Пробелы   и\tтабуляция",
    };

    for (const auto& sample : samples) {
        const std::string source = "{\"s\":\"" + json::escape(sample) + "\"}";
        const auto value = json::parse(source);
        const std::string text = json::serialize(value);

        CHECK(same(value, json::parse(text)));
    }
}

TEST_CASE(json_fuzz_mutation_of_valid_document) {
    const std::string source =
        R"({"v":1,"id":"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60","action":"GetStatus",)"
        R"("payload":{"taskId":"8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60"}})";

    XorShift32 random;

    for (int iteration = 0; iteration < 500; ++iteration) {
        std::string mutated = source;
        const int changes = 1 + static_cast<int>(random.next() % 3);
        for (int i = 0; i < changes; ++i) {
            const size_t position = random.next() % mutated.size();
            // Печатный ASCII: документ остаётся валидным UTF-8, поэтому
            // сериализация обязана быть идемпотентной.
            mutated[position] = static_cast<char>(0x20 + (random.next() % 0x5F));
        }

        const auto value = json::parse(mutated);
        const std::string text = json::serialize(value);
        const auto again = json::parse(text);

        CHECK(!text.empty());
        // Порядок ключей в unordered_map не гарантирован, поэтому сравниваем
        // структуру, а не текст.
        CHECK(same(value, again));
    }
}

TEST_CASE(json_string_limit_64k) {
    // IPC.md, п. 1: максимальная длина строки — 64 КиБ.
    const std::string big = repeat('a', 64 * 1024);
    const std::string source = "{\"s\":\"" + big + "\"}";

    const auto value = json::parse(source);
    REQUIRE(value.is_object());

    const auto* s = value.get("s");
    REQUIRE(s != nullptr);
    CHECK_EQ(s->as_string().size(), big.size());

    const auto restored = json::parse(json::serialize(value));
    const auto* restored_s = restored.get("s");
    REQUIRE(restored_s != nullptr);
    CHECK_EQ(restored_s->as_string(), big);
}

TEST_CASE(json_depth_limit_64) {
    // IPC.md, п. 1: максимальная глубина JSON — 64. Глубина 64 обязана разбираться.
    const int depth = 64;
    const std::string source = repeat('[', depth) + repeat(']', depth);

    const auto value = json::parse(source);
    CHECK(value.is_array());

    // Превышение лимита (65+) сейчас не отклоняется: харденинг — задача B-4.
    PENDING("B-4: лимит глубины 65+ и код TOO_LARGE проверяются на уровне IPC-сервера");
}

TEST_CASE(json_supplementary_plane_escape) {
    // Эмодзи (U+1F600) кодируется суррогатной парой. Текущий unescape собирает
    // CESU-8 вместо UTF-8, поэтому round-trip не совпадает. Дефект в зоне B.
    const std::string emoji = "\xF0\x9F\x98\x80";
    const std::string escaped = json::escape(emoji);

    CHECK_EQ(escaped, std::string("\\ud83d\\ude00"));
    PENDING("B-4: json::unescape не собирает суррогатную пару в UTF-8 (дефект вне зоны G)");
}
