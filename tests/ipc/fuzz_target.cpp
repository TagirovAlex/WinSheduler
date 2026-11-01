// Цель для libFuzzer (clang). В обычную сборку не входит.
// Пример запуска:
//   clang++ -std=c++20 -fsanitize=fuzzer,address -I./src/WinShedulerSvc \
//     ./src/WinShedulerSvc/json.cpp ./src/WinShedulerSvc/ipc_frame.cpp \
//     ./tests/ipc/fuzz_target.cpp -o ./temp/ipc_fuzz
#include "json.h"
#include "ipc_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);

    auto result = json::parse_ex(input);
    if (result.ok()) {
        // Сериализация разобранного значения не должна падать.
        (void)json::serialize(result.value);
    }

    ipc_frame::Envelope envelope;
    (void)ipc_frame::parse_envelope(input, envelope);
    return 0;
}
