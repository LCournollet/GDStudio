#pragma once

// Small self-contained JSON value used for presets and project files.
// The core library deliberately does not depend on Geode's matjson so that it
// can be built and tested without the game. Objects keep insertion order so
// written files are stable and diff-friendly.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gdds::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : m_data(b) {}
    Value(int v) : m_data(double(v)) {}
    Value(int64_t v) : m_data(double(v)) {}
    Value(uint64_t v) : m_data(double(v)) {}
    Value(unsigned v) : m_data(double(v)) {}
    Value(double v) : m_data(v) {}
    Value(float v) : m_data(double(v)) {}
    Value(char const* s) : m_data(std::string(s)) {}
    Value(std::string s) : m_data(std::move(s)) {}
    Value(std::string_view s) : m_data(std::string(s)) {}
    Value(Array a) : m_data(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : m_data(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const;
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isNumber() const { return type() == Type::Number; }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    bool asBool(bool def = false) const;
    double asDouble(double def = 0) const;
    int asInt(int def = 0) const { return isNumber() ? int(asDouble()) : def; }
    int64_t asInt64(int64_t def = 0) const { return isNumber() ? int64_t(asDouble()) : def; }
    uint64_t asUInt64(uint64_t def = 0) const;
    std::string asString(std::string const& def = "") const;

    // Array access
    Array const& items() const;
    Array& items();
    size_t size() const;
    void push(Value v);

    // Object access. operator[] on a non-object turns it into an object.
    Object const& members() const;
    Object& members();
    bool contains(std::string_view key) const;
    Value const* find(std::string_view key) const;
    Value const& get(std::string_view key) const;  // returns null if missing
    Value& operator[](std::string_view key);
    void set(std::string_view key, Value v) { (*this)[key] = std::move(v); }

    // Convenience typed getters with defaults.
    double num(std::string_view key, double def = 0) const { return get(key).asDouble(def); }
    int integer(std::string_view key, int def = 0) const { return get(key).asInt(def); }
    bool boolean(std::string_view key, bool def = false) const { return get(key).asBool(def); }
    std::string str(std::string_view key, std::string const& def = "") const { return get(key).asString(def); }

    std::string dump(int indent = 2) const;

    // Deep copy (arrays/objects are shared by reference internally).
    Value clone() const;

private:
    using Data = std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>>;
    Data m_data;
    void detach();
    void dumpTo(std::string& out, int indent, int depth) const;
};

struct ParseError {
    std::string message;
    size_t offset = 0;
};

// Returns the parsed value, or nullopt and fills `error`.
std::optional<Value> parse(std::string_view text, ParseError* error = nullptr);

}  // namespace gdds::json
