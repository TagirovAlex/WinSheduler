#pragma once
#include <cstdint>
#include <string>

// Кадрирование IPC v2: 4 байта длины (little-endian, uint32) + UTF-8 JSON.
// Контракт: ./docs/contracts/IPC.md, раздел 1.
namespace ipc_frame {

inline constexpr std::uint32_t MAX_FRAME_BYTES = 1024u * 1024u; // 1 МиБ
inline constexpr std::size_t HEADER_BYTES = 4;
inline constexpr std::size_t MAX_DEPTH = 64;
inline constexpr std::size_t MAX_STRING_BYTES = 64u * 1024u;

// Классификация начала потока: полноценный кадр v2 или legacy-сообщение
// без префикса длины (переходный режим, см. IPC.md раздел 7).
enum class Prefix {
    NeedMore, // данных меньше 4 байт
    Frame,    // корректный префикс длины
    Legacy    // префикса нет, читаем сообщение целиком
};

// Собирает кадр: 4 байта длины + тело.
std::string encode(const std::string& payload);

// Читает длину из заголовка (little-endian). false — заголовок некорректен.
bool decode_header(const unsigned char* header, std::size_t size, std::uint32_t& out_len);

// Определяет формат по первым байтам. Legacy-сообщение всегда начинается
// с '{' (0x7B), поэтому его «длина» выходит за пределы MAX_FRAME_BYTES
// и однозначно распознаётся как legacy.
Prefix classify_prefix(const unsigned char* data, std::size_t size, std::uint32_t& frame_len);

// Разобранный конверт запроса. Все строки — UTF-8.
struct Envelope {
    bool legacy = false; // true — переходный режим {Action, Payload}
    std::string action;
    std::string id;      // GUID запроса; в legacy пусто
    std::string payload; // JSON-текст payload
};

enum class EnvelopeStatus {
    Ok = 0,
    Invalid,             // не объект, нет действия, битый JSON
    TooLarge,            // превышен размер/глубина/длина строки
    UnsupportedVersion   // v не равен 1
};

// Разбор конверта без побочных эффектов. Для legacy-режима строка Payload,
// содержащая JSON, разворачивается в этот JSON.
EnvelopeStatus parse_envelope(const std::string& body, Envelope& out);

} // namespace ipc_frame
