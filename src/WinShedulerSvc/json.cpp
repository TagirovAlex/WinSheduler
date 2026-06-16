#include "json.h"
#include <charconv>
#include <sstream>
#include <cctype>

namespace json {

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
            // Decode UTF-8 codepoint
            unsigned int codepoint = 0;
            int extra = 0;
            if ((c & 0xE0) == 0xC0) { codepoint = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { codepoint = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { codepoint = c & 0x07; extra = 3; }
            else { out += '?'; ++i; continue; }
            for (int j = 0; j < extra && i + 1 < s.size(); ++j) {
                ++i;
                unsigned char next = static_cast<unsigned char>(s[i]);
                if ((next & 0xC0) == 0x80) codepoint = (codepoint << 6) | (next & 0x3F);
            }
            ++i;
            if (codepoint <= 0xFFFF) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", codepoint);
                out += buf;
            } else {
                // Supplementary plane: surrogate pair
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
                if (i + 4 < s.size()) {
                    unsigned int codepoint;
                    auto [_, ec] = std::from_chars(s.data() + i + 1, s.data() + i + 5, codepoint, 16);
                    if (ec == std::errc()) {
                        if (codepoint < 0x80) {
                            out += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            out += static_cast<char>(0xC0 | (codepoint >> 6));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else if (codepoint < 0x10000) {
                            out += static_cast<char>(0xE0 | (codepoint >> 12));
                            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            // Supplementary plane
                            codepoint -= 0x10000;
                            unsigned int hi = 0xD800 + (codepoint >> 10);
                            unsigned int lo = 0xDC00 + (codepoint & 0x3FF);
                            out += static_cast<char>(0xF0 | (hi >> 18));
                            out += static_cast<char>(0x80 | ((hi >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((hi >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (hi & 0x3F));
                            out += static_cast<char>(0xF0 | (lo >> 18));
                            out += static_cast<char>(0x80 | ((lo >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((lo >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (lo & 0x3F));
                        }
                        i += 4;
                    }
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

struct Parser {
    const std::string& input;
    size_t pos = 0;

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
        if (!expect('"')) return "";
        std::string result;
        while (pos < input.size()) {
            char c = input[pos++];
            if (c == '"') return result;
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
                    if (pos + 4 <= input.size()) {
                        unsigned int codepoint;
                        auto [_, ec] = std::from_chars(input.data() + pos, input.data() + pos + 4, codepoint, 16);
                        if (ec == std::errc()) {
                            if (codepoint < 128) result += static_cast<char>(codepoint);
                            else result += '?';
                            pos += 4;
                        }
                    }
                    break;
                }
                default: result += next; break;
                }
            } else {
                result += c;
            }
        }
        return result;
    }

    Value parse_value() {
        char c = peek();
        if (c == '"') return Value(parse_string());
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '\0') return Value(nullptr); // unexpected end
        return parse_number();
    }

    Value parse_object() {
        Object obj;
        if (!expect('{')) return obj;
        if (peek() == '}') { advance(); return obj; }
        while (pos < input.size()) {
            auto key = parse_string();
            if (key.empty() && peek() != ':') break; // no progress
            if (!expect(':')) break;
            obj[key] = parse_value();
            if (peek() != ',') break;
            advance(); // skip ','
        }
        expect('}');
        return obj;
    }

    Value parse_array() {
        Array arr;
        if (!expect('[')) return arr;
        if (peek() == ']') { advance(); return arr; }
        while (pos < input.size()) {
            size_t prev = pos;
            arr.push_back(parse_value());
            if (pos == prev) break; // no progress
            if (peek() != ',') break;
            advance(); // skip ','
        }
        expect(']');
        return arr;
    }

    Value parse_bool() {
        if (input.compare(pos, 4, "true") == 0) { pos += 4; return Value(true); }
        if (input.compare(pos, 5, "false") == 0) { pos += 5; return Value(false); }
        return Value(false);
    }

    Value parse_null() {
        if (input.compare(pos, 4, "null") == 0) { pos += 4; return Value(nullptr); }
        return Value(nullptr);
    }

    Value parse_number() {
        skip_ws();
        size_t start = pos;
        if (pos < input.size() && input[pos] == '-') ++pos;
        while (pos < input.size() && std::isdigit(input[pos])) ++pos;
        if (pos < input.size() && input[pos] == '.') {
            ++pos;
            while (pos < input.size() && std::isdigit(input[pos])) ++pos;
        }
        if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
            ++pos;
            if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
            while (pos < input.size() && std::isdigit(input[pos])) ++pos;
        }
        double val = 0.0;
        std::from_chars(input.data() + start, input.data() + pos, val);
        return Value(val);
    }
};

Value parse(const std::string& input) {
    Parser p{ input, 0 };
    return p.parse_value();
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
