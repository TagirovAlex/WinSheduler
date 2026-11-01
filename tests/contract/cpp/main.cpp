// Точка входа контрактных тестов C++.
//
// Использование:
//   WinSheduler.ContractTests.exe --fixtures=<каталог> [--filter=<подстрока>]
// Возвращает 0, если все тесты прошли, и 1 при любом провале.

#include "test_harness.h"

#include <cstring>
#include <string>

namespace {

bool starts_with(const char* value, const char* prefix) {
    return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

} // namespace

int main(int argc, char** argv) {
    const char* filter = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (starts_with(argv[i], "--fixtures=")) {
            test::fixtures_dir() = argv[i] + std::strlen("--fixtures=");
        }
        else if (starts_with(argv[i], "--filter=")) {
            filter = argv[i] + std::strlen("--filter=");
        }
    }

    int run = 0;
    int failed_cases = 0;

    for (const auto& item : test::registry()) {
        if (filter != nullptr && std::strstr(item.name, filter) == nullptr) {
            continue;
        }

        ++run;
        const int before = test::failure_count();

        std::printf("[ RUN  ] %s\n", item.name);
        try {
            item.fn();
        }
        catch (const test::Abort&) {
            // REQUIRE уже записал причину.
        }
        catch (const std::exception& error) {
            test::report(item.name, 0, std::string("исключение: ") + error.what());
        }
        catch (...) {
            test::report(item.name, 0, "неизвестное исключение");
        }

        if (test::failure_count() != before) {
            ++failed_cases;
            std::printf("[FAIL ] %s\n", item.name);
        }
        else {
            std::printf("[ OK  ] %s\n", item.name);
        }
    }

    std::printf("\nТестов запущено: %d, провалено: %d, проверок: %d\n",
        run, failed_cases, test::check_count());

    if (run == 0) {
        std::printf("Не найдено ни одного теста.\n");
        return 1;
    }

    if (failed_cases > 0 || test::failure_count() > 0) {
        std::printf("Контрактные тесты C++ провалены.\n");
        return 1;
    }

    std::printf("Контрактные тесты C++ пройдены.\n");
    return 0;
}
