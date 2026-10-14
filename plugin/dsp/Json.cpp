#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <xlocale.h>
#endif
#include <locale.h>

namespace cymbalis::json
{

namespace
{
const char* typeName(Value::Type t)
{
    switch (t)
    {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return "bool";
        case Value::Type::Number: return "number";
        case Value::Type::String: return "string";
        case Value::Type::Array: return "array";
        case Value::Type::Object: return "object";
    }
    return "?";
}

[[noreturn]] void typeError(Value::Type want, Value::Type got)
{
    throw ParseError(std::string("JSON type error: expected ") + typeName(want) + ", got " + typeName(got));
}

// Locale-independent, correctly rounded string -> double.
double parseDouble(const std::string& token)
{
    static locale_t cLocale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    char* end = nullptr;
    const double v = strtod_l(token.c_str(), &end, cLocale);
    if (end == token.c_str() || *end != '\0')
        throw ParseError("JSON: bad number '" + token + "'");
    return v;
}

void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
} // namespace

double Value::number() const
{
    if (type_ != Type::Number) typeError(Type::Number, type_);
    return number_;
}

bool Value::boolean() const
{
    if (type_ != Type::Bool) typeError(Type::Bool, type_);
    return bool_;
}

const std::string& Value::string() const
{
    if (type_ != Type::String) typeError(Type::String, type_);
    return string_;
}

const std::vector<Value>& Value::array() const
{
    if (type_ != Type::Array) typeError(Type::Array, type_);
    return array_;
}

const std::vector<std::pair<std::string, Value>>& Value::object() const
{
    if (type_ != Type::Object) typeError(Type::Object, type_);
    return object_;
}

const Value* Value::find(const std::string& key) const
{
    for (const auto& kv : object())
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value& Value::at(const std::string& key) const
{
    if (const Value* v = find(key)) return *v;
    throw ParseError("JSON: missing key '" + key + "'");
}

class Parser
{
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Value parseDocument()
    {
        Value v = parseValue(0);
        skipWs();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const
    {
        throw ParseError("JSON parse error at offset " + std::to_string(pos_) + ": " + what);
    }

    void skipWs()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t'))
            ++pos_;
    }

    bool consume(char c)
    {
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    void expectLiteral(const char* lit)
    {
        for (const char* p = lit; *p; ++p, ++pos_)
            if (pos_ >= s_.size() || s_[pos_] != *p) fail(std::string("expected ") + lit);
    }

    Value parseValue(int depth)
    {
        if (depth > 64) fail("nesting too deep");
        skipWs();
        if (pos_ >= s_.size()) fail("unexpected end");
        const char c = s_[pos_];
        Value v;
        if (c == '{')
        {
            ++pos_;
            v.type_ = Value::Type::Object;
            if (consume('}')) return v;
            do
            {
                skipWs();
                if (pos_ >= s_.size() || s_[pos_] != '"') fail("expected object key");
                std::string key = parseString();
                if (!consume(':')) fail("expected ':'");
                v.object_.emplace_back(std::move(key), parseValue(depth + 1));
            } while (consume(','));
            if (!consume('}')) fail("expected '}'");
        }
        else if (c == '[')
        {
            ++pos_;
            v.type_ = Value::Type::Array;
            if (consume(']')) return v;
            do
                v.array_.push_back(parseValue(depth + 1));
            while (consume(','));
            if (!consume(']')) fail("expected ']'");
        }
        else if (c == '"')
        {
            v.type_ = Value::Type::String;
            v.string_ = parseString();
        }
        else if (c == 't')
        {
            expectLiteral("true");
            v.type_ = Value::Type::Bool;
            v.bool_ = true;
        }
        else if (c == 'f')
        {
            expectLiteral("false");
            v.type_ = Value::Type::Bool;
            v.bool_ = false;
        }
        else if (c == 'n')
        {
            expectLiteral("null");
        }
        else if (c == '-' || (c >= '0' && c <= '9'))
        {
            const size_t start = pos_;
            while (pos_ < s_.size())
            {
                const char d = s_[pos_];
                if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') ++pos_;
                else break;
            }
            v.type_ = Value::Type::Number;
            v.number_ = parseDouble(s_.substr(start, pos_ - start));
        }
        else
        {
            fail(std::string("unexpected character '") + c + "'");
        }
        return v;
    }

    unsigned parseHex4()
    {
        if (pos_ + 4 > s_.size()) fail("short \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = s_[pos_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string parseString()
    {
        ++pos_; // opening quote
        std::string out;
        while (true)
        {
            if (pos_ >= s_.size()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') break;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) fail("bad escape");
            const char e = s_[pos_++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned cp = parseHex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u')
                    {
                        pos_ += 2;
                        const unsigned lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }
};

Value parse(const std::string& text)
{
    return Parser(text).parseDocument();
}

Value parseFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw ParseError("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    try
    {
        return parse(ss.str());
    }
    catch (const ParseError& e)
    {
        throw ParseError(path + ": " + e.what());
    }
}

bool equal(const Value& a, const Value& b)
{
    if (a.type() != b.type()) return false;
    switch (a.type())
    {
        case Value::Type::Null: return true;
        case Value::Type::Bool: return a.boolean() == b.boolean();
        case Value::Type::Number: return a.number() == b.number();
        case Value::Type::String: return a.string() == b.string();
        case Value::Type::Array:
        {
            const auto& x = a.array();
            const auto& y = b.array();
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (!equal(x[i], y[i])) return false;
            return true;
        }
        case Value::Type::Object:
        {
            const auto& x = a.object();
            if (x.size() != b.object().size()) return false;
            for (const auto& kv : x)
            {
                const Value* other = b.find(kv.first);
                if (other == nullptr || !equal(kv.second, *other)) return false;
            }
            return true;
        }
    }
    return false;
}

std::string toString(const Value& v)
{
    switch (v.type())
    {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return v.boolean() ? "true" : "false";
        case Value::Type::Number:
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", v.number());
            return buf;
        }
        case Value::Type::String: return "\"" + v.string() + "\"";
        case Value::Type::Array:
        {
            std::string out = "[";
            for (size_t i = 0; i < v.array().size(); ++i)
                out += (i ? "," : "") + toString(v.array()[i]);
            return out + "]";
        }
        case Value::Type::Object:
        {
            std::string out = "{";
            bool first = true;
            for (const auto& kv : v.object())
            {
                out += (first ? "\"" : ",\"") + kv.first + "\":" + toString(kv.second);
                first = false;
            }
            return out + "}";
        }
    }
    return "?";
}

} // namespace cymbalis::json
