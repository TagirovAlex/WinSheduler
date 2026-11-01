#pragma once
// Минимальный тест-харнесс без внешних зависимостей.
//
// Тесты регистрируются макросом TEST_CASE, проверки выполняются макросами
// CHECK/CHECK_EQ/REQUIRE. Харнесс сознательно не использует GoogleTest и
// Catch2: новые зависимости требуют ADR (./docs/plan/AGENTS.md, раздел 3).

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

// Реестр тестов: заполняется статическими регистраторами при загрузке модуля.
inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int value = 0;
    return value;
}

inline int& check_count() {
    static int value = 0;
    return value;
}

// Каталог с общими фикстурами (./tests/contract/fixtures). Задаётся в main.
inline std::string& fixtures_dir() {
    static std::string value = "tests/contract/fixtures";
    return value;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(Case{ name, fn }); }
};

// Исключение REQUIRE: прерывает текущий тест, но не весь прогон.
struct Abort {};

inline void report(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::printf("    [FAIL] %s:%d: %s\n", file, line, message.c_str());
}

inline std::string to_text(const std::string& value) {
    return "\"" + value + "\"";
}

inline std::string to_text(const char* value) {
    return std::string("\"") + (value ? value : "nullptr") + "\"";
}

inline std::string to_text(bool value) {
    return value ? "true" : "false";
}

inline std::string to_text(std::nullptr_t) {
    return "null";
}

template <typename T>
std::string to_text(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// Отложенная проверка: контракт зафиксирован, реализация ещё не готова.
// Не валит gate, но печатает причину, чтобы долг не потерялся.
inline void pending(const char* reason) {
    std::printf("    [PENDING] %s\n", reason);
}

} // namespace test

#define TEST_CASE(name)                                        \
    static void name();                                        \
    static ::test::Registrar registrar_##name(#name, name);    \
    static void name()

#define CHECK(condition)                                                       \
    do {                                                                       \
        ++::test::check_count();                                               \
        if (!(condition)) {                                                    \
            ::test::report(__FILE__, __LINE__,                                 \
                std::string("условие не выполнено: ") + #condition);          \
        }                                                                      \
    } while (0)

#define CHECK_MSG(condition, message)                                          \
    do {                                                                       \
        ++::test::check_count();                                               \
        if (!(condition)) {                                                    \
            ::test::report(__FILE__, __LINE__,                                 \
                std::string(message) + ": " + #condition);                     \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        ++::test::check_count();                                               \
        const auto& actual_value_ = (actual);                                  \
        const auto& expected_value_ = (expected);                              \
        if (!(actual_value_ == expected_value_)) {                             \
            ::test::report(__FILE__, __LINE__,                                 \
                std::string("ожидалось ") + ::test::to_text(expected_value_) + \
                ", получено " + ::test::to_text(actual_value_));               \
        }                                                                      \
    } while (0)

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        ++::test::check_count();                                               \
        if (!(condition)) {                                                    \
            ::test::report(__FILE__, __LINE__,                                 \
                std::string("обязательное условие не выполнено: ") + #condition); \
            throw ::test::Abort{};                                             \
        }                                                                      \
    } while (0)

#define PENDING(reason) ::test::pending(reason)
