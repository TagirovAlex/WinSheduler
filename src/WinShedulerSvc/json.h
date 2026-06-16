#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace json {

struct Value;
using Object = std::unordered_map<std::string, Value>;
using Array = std::vector<Value>;
using ValueData = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

struct Value {
    ValueData data;

    Value() : data(nullptr) {}
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool v) : data(v) {}
    Value(double v) : data(v) {}
    Value(int v) : data(static_cast<double>(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(const std::string& v) : data(v) {}
    Value(std::string&& v) : data(std::move(v)) {}
    Value(const Array& v) : data(v) {}
    Value(Array&& v) : data(std::move(v)) {}
    Value(const Object& v) : data(v) {}
    Value(Object&& v) : data(std::move(v)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }

    bool as_bool() const { return std::get<bool>(data); }
    double as_number() const { return std::get<double>(data); }
    int as_int() const { return static_cast<int>(std::get<double>(data)); }
    const std::string& as_string() const { return std::get<std::string>(data); }
    const Array& as_array() const { return std::get<Array>(data); }
    const Object& as_object() const { return std::get<Object>(data); }

    const Value* get(const std::string& key) const {
        if (!is_object()) return nullptr;
        auto& obj = std::get<Object>(data);
        auto it = obj.find(key);
        return it != obj.end() ? &it->second : nullptr;
    }

    std::string str() const; // forward decl
};

// Parsing
Value parse(const std::string& input);
std::string serialize(const Value& v);

// Helpers for IPC
std::string escape(const std::string& s);
std::string unescape(const std::string& s);

} // namespace json
