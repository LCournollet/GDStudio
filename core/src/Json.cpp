#include <gdds/Json.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>

namespace gdds::json {

namespace {
Value const kNull;
Array const kEmptyArray;
Object const kEmptyObject;
}

Value::Type Value::type() const {
    switch (m_data.index()) {
        case 0: return Type::Null;
        case 1: return Type::Bool;
        case 2: return Type::Number;
        case 3: return Type::String;
        case 4: return Type::Array;
        default: return Type::Object;
    }
}

bool Value::asBool(bool def) const {
    if (auto b = std::get_if<bool>(&m_data)) return *b;
    if (auto d = std::get_if<double>(&m_data)) return *d != 0;
    return def;
}

double Value::asDouble(double def) const {
    if (auto d = std::get_if<double>(&m_data)) return *d;
    if (auto b = std::get_if<bool>(&m_data)) return *b ? 1 : 0;
    return def;
}

uint64_t Value::asUInt64(uint64_t def) const {
    // Large ids are stored as strings to survive the double round trip.
    if (auto s = std::get_if<std::string>(&m_data)) {
        uint64_t v = 0;
        auto res = std::from_chars(s->data(), s->data() + s->size(), v);
        return res.ec == std::errc() ? v : def;
    }
    if (auto d = std::get_if<double>(&m_data)) return uint64_t(*d);
    return def;
}

std::string Value::asString(std::string const& def) const {
    if (auto s = std::get_if<std::string>(&m_data)) return *s;
    return def;
}

void Value::detach() {
    if (auto a = std::get_if<std::shared_ptr<Array>>(&m_data)) {
        if (a->use_count() > 1) m_data = std::make_shared<Array>(**a);
    }
    else if (auto o = std::get_if<std::shared_ptr<Object>>(&m_data)) {
        if (o->use_count() > 1) m_data = std::make_shared<Object>(**o);
    }
}

Array const& Value::items() const {
    if (auto a = std::get_if<std::shared_ptr<Array>>(&m_data)) return **a;
    return kEmptyArray;
}

Array& Value::items() {
    if (!isArray()) m_data = std::make_shared<Array>();
    detach();
    return *std::get<std::shared_ptr<Array>>(m_data);
}

size_t Value::size() const {
    if (isArray()) return items().size();
    if (isObject()) return members().size();
    return 0;
}

void Value::push(Value v) { items().push_back(std::move(v)); }

Object const& Value::members() const {
    if (auto o = std::get_if<std::shared_ptr<Object>>(&m_data)) return **o;
    return kEmptyObject;
}

Object& Value::members() {
    if (!isObject()) m_data = std::make_shared<Object>();
    detach();
    return *std::get<std::shared_ptr<Object>>(m_data);
}

bool Value::contains(std::string_view key) const { return find(key) != nullptr; }

Value const* Value::find(std::string_view key) const {
    for (auto& [k, v] : members()) if (k == key) return &v;
    return nullptr;
}

Value const& Value::get(std::string_view key) const {
    auto v = find(key);
    return v ? *v : kNull;
}

Value& Value::operator[](std::string_view key) {
    auto& m = members();
    for (auto& [k, v] : m) if (k == key) return v;
    m.emplace_back(std::string(key), Value());
    return m.back().second;
}

Value Value::clone() const {
    if (isArray()) {
        Array out;
        for (auto& v : items()) out.push_back(v.clone());
        return Value(std::move(out));
    }
    if (isObject()) {
        Object out;
        for (auto& [k, v] : members()) out.emplace_back(k, v.clone());
        return Value(std::move(out));
    }
    return *this;
}

namespace {

void escapeString(std::string& out, std::string const& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else out += char(c);
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "0"; return; }
    if (d == std::floor(d) && std::abs(d) < 1e15) {
        out += std::to_string(int64_t(d));
        return;
    }
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);
    out.append(buf, res.ptr);
}

}  // namespace

void Value::dumpTo(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        if (indent <= 0) return;
        out += '\n';
        out.append(size_t(indent * d), ' ');
    };
    switch (type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += asBool() ? "true" : "false"; break;
        case Type::Number: writeNumber(out, asDouble()); break;
        case Type::String: escapeString(out, std::get<std::string>(m_data)); break;
        case Type::Array: {
            auto& a = items();
            if (a.empty()) { out += "[]"; break; }
            // Arrays of scalars stay on one line to keep files compact.
            bool scalars = true;
            for (auto& v : a) if (v.isArray() || v.isObject()) { scalars = false; break; }
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) out += scalars && indent > 0 ? ", " : ",";
                if (!scalars) newline(depth + 1);
                a[i].dumpTo(out, indent, depth + 1);
            }
            if (!scalars) newline(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            auto& o = members();
            if (o.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < o.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                escapeString(out, o[i].first);
                out += indent > 0 ? ": " : ":";
                o[i].second.dumpTo(out, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            break;
        }
    }
}

std::string Value::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Parser

namespace {

struct Parser {
    std::string_view s;
    size_t i = 0;
    ParseError err;
    bool failed = false;

    void fail(std::string msg) {
        if (!failed) { failed = true; err = {std::move(msg), i}; }
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool consume(char c) {
        ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
        else {
            out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t& out) {
        if (i + 4 > s.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') out |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    std::optional<std::string> parseString() {
        if (!consume('"')) { fail("expected string"); return std::nullopt; }
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) { fail("bad \\u escape"); return std::nullopt; }
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo;
                        if (!hex4(lo)) { fail("bad surrogate"); return std::nullopt; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape"); return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> parseValue(int depth) {
        if (depth > 256) { fail("nesting too deep"); return std::nullopt; }
        ws();
        if (i >= s.size()) { fail("unexpected end"); return std::nullopt; }
        char c = s[i];
        if (c == '{') {
            ++i;
            Value obj = Value::object();
            if (consume('}')) return obj;
            while (true) {
                auto key = parseString();
                if (!key) return std::nullopt;
                if (!consume(':')) { fail("expected ':'"); return std::nullopt; }
                auto v = parseValue(depth + 1);
                if (!v) return std::nullopt;
                obj.members().emplace_back(std::move(*key), std::move(*v));
                if (consume(',')) continue;
                if (consume('}')) return obj;
                fail("expected ',' or '}'");
                return std::nullopt;
            }
        }
        if (c == '[') {
            ++i;
            Value arr = Value::array();
            if (consume(']')) return arr;
            while (true) {
                auto v = parseValue(depth + 1);
                if (!v) return std::nullopt;
                arr.items().push_back(std::move(*v));
                if (consume(',')) continue;
                if (consume(']')) return arr;
                fail("expected ',' or ']'");
                return std::nullopt;
            }
        }
        if (c == '"') {
            auto str = parseString();
            if (!str) return std::nullopt;
            return Value(std::move(*str));
        }
        if (s.substr(i, 4) == "true") { i += 4; return Value(true); }
        if (s.substr(i, 5) == "false") { i += 5; return Value(false); }
        if (s.substr(i, 4) == "null") { i += 4; return Value(); }
        // number
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) ++i;
        if (start == i) { fail("unexpected character"); return std::nullopt; }
        double d = 0;
        auto first = s.data() + start;
        if (*first == '+') ++first;
        auto res = std::from_chars(first, s.data() + i, d);
        if (res.ec != std::errc()) { fail("bad number"); return std::nullopt; }
        return Value(d);
    }
};

}  // namespace

std::optional<Value> parse(std::string_view text, ParseError* error) {
    Parser p{text};
    auto v = p.parseValue(0);
    if (v) {
        p.ws();
        if (p.i != text.size()) { p.fail("trailing characters"); v.reset(); }
    }
    if (!v && error) *error = p.err;
    return v;
}

}  // namespace gdds::json
