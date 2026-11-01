#include "ipc_frame.h"
#include "json.h"

namespace ipc_frame {

std::string encode(const std::string& payload) {
    std::string out;
    out.resize(HEADER_BYTES + payload.size());
    const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    out[0] = static_cast<char>(len & 0xFF);
    out[1] = static_cast<char>((len >> 8) & 0xFF);
    out[2] = static_cast<char>((len >> 16) & 0xFF);
    out[3] = static_cast<char>((len >> 24) & 0xFF);
    if (!payload.empty()) out.replace(HEADER_BYTES, payload.size(), payload);
    return out;
}

bool decode_header(const unsigned char* header, std::size_t size, std::uint32_t& out_len) {
    if (header == nullptr || size < HEADER_BYTES) return false;
    out_len = static_cast<std::uint32_t>(header[0]) |
              (static_cast<std::uint32_t>(header[1]) << 8) |
              (static_cast<std::uint32_t>(header[2]) << 16) |
              (static_cast<std::uint32_t>(header[3]) << 24);
    return true;
}

Prefix classify_prefix(const unsigned char* data, std::size_t size, std::uint32_t& frame_len) {
    if (data == nullptr || size < HEADER_BYTES) return Prefix::NeedMore;
    std::uint32_t len = 0;
    if (!decode_header(data, size, len)) return Prefix::NeedMore;
    // Нулевая длина и значения больше лимита кадра означают legacy-сообщение.
    if (len == 0 || len > MAX_FRAME_BYTES) {
        frame_len = 0;
        return Prefix::Legacy;
    }
    frame_len = len;
    return Prefix::Frame;
}

namespace {

std::string string_field(const json::Value& obj, const char* name) {
    const json::Value* value = obj.get(name);
    if (value == nullptr || !value->is_string()) return {};
    return value->as_string();
}

} // namespace

EnvelopeStatus parse_envelope(const std::string& body, Envelope& out) {
    out = Envelope{};

    json::Limits limits;
    limits.max_bytes = MAX_FRAME_BYTES;
    limits.max_depth = MAX_DEPTH;
    limits.max_string_bytes = MAX_STRING_BYTES;

    auto parsed = json::parse_ex(body, limits);
    if (!parsed.ok()) {
        switch (parsed.status) {
        case json::ParseStatus::TooLarge:
        case json::ParseStatus::TooDeep:
        case json::ParseStatus::StringTooLong:
            return EnvelopeStatus::TooLarge;
        default:
            return EnvelopeStatus::Invalid;
        }
    }
    if (!parsed.value.is_object()) return EnvelopeStatus::Invalid;
    const json::Value& root = parsed.value;

    const json::Value* version = root.get("v");
    if (version != nullptr) {
        if (!version->is_number() || version->as_int() != 1) {
            return EnvelopeStatus::UnsupportedVersion;
        }
        out.legacy = false;
        out.action = string_field(root, "action");
        out.id = string_field(root, "id");
        const json::Value* payload = root.get("payload");
        out.payload = payload != nullptr ? json::serialize(*payload) : "{}";
    } else {
        // Переходный режим {Action, Payload}, IPC.md раздел 7.
        out.legacy = true;
        out.action = string_field(root, "Action");
        const json::Value* payload = root.get("Payload");
        if (payload == nullptr) {
            out.payload = "{}";
        } else if (payload->is_string()) {
            // Старый клиент передаёт payload строкой, внутри которой JSON.
            auto inner = json::parse_ex(payload->as_string(), limits);
            out.payload = inner.ok() ? payload->as_string() : json::serialize(*payload);
        } else {
            out.payload = json::serialize(*payload);
        }
    }

    if (out.action.empty()) return EnvelopeStatus::Invalid;
    return EnvelopeStatus::Ok;
}

} // namespace ipc_frame
