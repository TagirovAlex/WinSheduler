#include "json.h"
#include <charconv>
#include <sstream>
#include <cctype>

namespace json {

// Кодирует один codepoint в UTF-8 (до 0x10FFFF).
static void append_utf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Разбирает 4 hex-цифры. Возвращает false, если цифры некорректны.
static bool parse_hex4(const std::string& s, std::size_t pos, unsigned int& out) {
    if (pos + 4 > s.size()) return false;
    unsigned int value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        char c = s[pos + i];
        unsigned int digit;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned int>(c - 'A' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            // ASCII
            switch (s[i]) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += s[i];
                }
            }
            ++i;
        } else {
            // Декодирование UTF-8. Битый lead-байт заменяется на '?', а
            // следующие байты обрабатываются независимо: continuation-байт
            // потребляется только после проверки.
            unsigned int codepoint = 0;
            int extra = 0;
            if ((c & 0xE0) == 0xC0) { codepoint = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { codepoint = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { codepoint = c & 0x07; extra = 3; }
            else { out += '?'; ++i; continue; }

            bool valid = true;
            for (int j = 0; j < extra; ++j) {
                if (i + 1 >= s.size()) { valid = false; break; }
                unsigned char next = static_cast<unsigned char>(s[i + 1]);
                if ((next & 0xC0) != 0x80) { valid = false; break; }
                ++i;
                codepoint = (codepoint << 6) | (next & 0x3F);
            }
            ++i; // lead-байт
            if (!valid) { out += '?'; continue; }

            if (codepoint <= 0xFFFF) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", codepoint);
                out += buf;
            } else {
                // Дополнительная плоскость: пара суррогатов.
                codepoint -= 0x10000;
                char buf[16];
                snprintf(buf, sizeof(buf), "\\u%04x\\u%04x",
                    0xD800 + (codepoint >> 10), 0xDC00 + (codepoint & 0x3FF));
                out += buf;
            }
        }
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[++i]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned int codepoint = 0;
                if (parse_hex4(s, i + 1, codepoint)) {
                    i += 4;
                    // Пара суррогатов \uD800-\uDBFF + \uDC00-\uDFFF → один codepoint.
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                        i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        unsigned int low = 0;
                        if (parse_hex4(s, i + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                            i += 6;
                        }
                    }
                    append_utf8(out, codepoint);
                }
                break;
            }
            default: out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Разборщик с контролем лимитов контракта. Ошибка фиксируется в status,
// после первой ошибки разбор прекращается — так исключается переполнение стека
// и неограниченный рост памяти на враждебном вводе.
struct Parser {
    const std::string& input;
    const Limits& limits;
    size_t pos = 0;
    size_t depth = 0;
    ParseStatus status = ParseStatus::Ok;

    void fail(ParseStatus s) {
        if (status == ParseStatus::Ok) status = s;
    }

    bool stopped() const { return status != ParseStatus::Ok; }

    void skip_ws() {
        while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n' || input[pos] == '\r'))
            ++pos;
    }

    char peek() { skip_ws(); return pos < input.size() ? input[pos] : '\0'; }
    char advance() { return pos < input.size() ? input[pos++] : '\0'; }

    bool expect(char c) {
        skip_ws();
        if (pos < input.size() && input[pos] == c) { ++pos; return true; }
        return false;
    }

    std::string parse_string() {
        if (!expect('"')) { fail(ParseStatus::Syntax); return ""; }
        std::string result;
        while (pos < input.size()) {
            char c = input[pos++];
            if (c == '"') {
                if (result.size() > limits.max_string_bytes) fail(ParseStatus::StringTooLong);
                return result;
            }
            if (c == '\\' && pos < input.size()) {
                char next = input[pos++];
                switch (next) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    unsigned int codepoint = 0;
                    if (parse_hex4(input, pos, codepoint)) {
                        pos += 4;
                        // Пара суррогатов → один codepoint UTF-8.
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                            pos + 6 <= input.size() && input[pos] == '\\' && input[pos + 1] == 'u') {
                            unsigned int low = 0;
                            if (parse_hex4(input, pos + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                pos += 6;
                            }
                        }
                        append_utf8(result, codepoint);
                    }
                    break;
                }
                default: result += next; break;
                }
            } else {
                result += c;
            }
            if (result.size() > limits.max_string_bytes) {
                fail(ParseStatus::StringTooLong);
                return {};
            }
        }
        // Строка не закрыта — считаем это синтаксической ошибкой.
        fail(ParseStatus::Syntax);
        return result;
    }

    Value parse_value() {
        if (stopped()) return Value(nullptr);
        char c = peek();
        if (c == '"') return Value(parse_string());
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '\0') { fail(ParseStatus::Syntax); return Value(nullptr); }
        return parse_number();
    }

    Value parse_object() {
        Object obj;
        if (!expect('{')) { fail(ParseStatus::Syntax); return obj; }
        if (++depth > limits.max_depth) { fail(ParseStatus::TooDeep); --depth; return obj; }
        if (peek() == '}') { advance(); --depth; return obj; }
        while (pos < input.size()) {
            auto key = parse_string();
            if (stopped()) break;
            if (!expect(':')) { fail(ParseStatus::Syntax); break; }
            obj[key] = parse_value();
            if (stopped()) break;
            if (peek() != ',') break;
            advance(); // пропуск ','
        }
        if (!expect('}')) fail(ParseStatus::Syntax);
        --depth;
        return obj;
    }

    Value parse_array() {
        Array arr;
        if (!expect('[')) { fail(ParseStatus::Syntax); return arr; }
        if (++depth > limits.max_depth) { fail(ParseStatus::TooDeep); --depth; return arr; }
        if (peek() == ']') { advance(); --depth; return arr; }
        while (pos < input.size()) {
            size_t prev = pos;
            arr.push_back(parse_value());
            if (stopped()) break;
            if (pos == prev) { fail(ParseStatus::Syntax); break; } // нет продвижения
            if (peek() != ',') break;
            advance(); // пропуск ','
        }
        if (!expect(']')) fail(ParseStatus::Syntax);
        --depth;
        return arr;
    }

    Value parse_bool() {
        if (input.compare(pos, 4, "true") == 0) { pos += 4; return Value(true); }
        if (input.compare(pos, 5, "false") == 0) { pos += 5; return Value(false); }
        fail(ParseStatus::Syntax);
        return Value(false);
    }

    Value parse_null() {
        if (input.compare(pos, 4, "null") == 0) { pos += 4; return Value(nullptr); }
        fail(ParseStatus::Syntax);
        return Value(nullptr);
    }

    Value parse_number() {
        skip_ws();
        size_t start = pos;
        if (pos < input.size() && input[pos] == '-') ++pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (pos < input.size() && input[pos] == '.') {
            ++pos;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        }
        if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
            ++pos;
            if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        }
        if (pos == start) { fail(ParseStatus::Syntax); return Value(nullptr); }
        double val = 0.0;
        auto [ptr, ec] = std::from_chars(input.data() + start, input.data() + pos, val);
        if (ec != std::errc() || ptr != input.data() + pos) { fail(ParseStatus::Syntax); return Value(nullptr); }
        return Value(val);
    }
};

ParseResult parse_ex(const std::string& input, const Limits& limits) {
    ParseResult result;
    if (input.empty()) {
        result.status = ParseStatus::Empty;
        return result;
    }
    if (input.size() > limits.max_bytes) {
        result.status = ParseStatus::TooLarge;
        return result;
    }
    Parser p{ input, limits };
    result.value = p.parse_value();
    // Мусор после корневого значения — синтаксическая ошибка.
    if (p.status == ParseStatus::Ok) {
        p.skip_ws();
        if (p.pos != input.size()) p.status = ParseStatus::Syntax;
    }
    result.status = p.status;
    return result;
}

Value parse(const std::string& input) {
    return parse_ex(input).value;
}

void serialize_value(const Value& v, std::ostringstream& out) {
    switch (v.data.index()) {
    case 0: out << "null"; break;
    case 1: out << (std::get<bool>(v.data) ? "true" : "false"); break;
    case 2: {
        double d = std::get<double>(v.data);
        if (d == static_cast<int>(d))
            out << static_cast<int>(d);
        else
            out << d;
        break;
    }
    case 3:
        out << '"' << escape(std::get<std::string>(v.data)) << '"';
        break;
    case 4: {
        out << '[';
        bool first = true;
        for (auto& elem : std::get<Array>(v.data)) {
            if (!first) out << ',';
            first = false;
            serialize_value(elem, out);
        }
        out << ']';
        break;
    }
    case 5: {
        out << '{';
        bool first = true;
        for (auto& [key, val] : std::get<Object>(v.data)) {
            if (!first) out << ',';
            first = false;
            out << '"' << escape(key) << "\":";
            serialize_value(val, out);
        }
        out << '}';
        break;
    }
    }
}

std::string serialize(const Value& v) {
    std::ostringstream out;
    serialize_value(v, out);
    return out.str();
}

} // namespace json
