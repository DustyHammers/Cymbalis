// Json.h — minimal JSON reader for the baked engine data and test sequences.
//
// Numbers are converted with strtod in the "C" locale, independent of the host's
// locale. strtod is correctly rounded, and Python writes floats with the shortest
// round-trip repr, so every baked double is parsed bit-exactly.
// Bake::load verifies this with per-array checksums.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cymbalis::json
{

struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }
    bool isBool() const noexcept { return type_ == Type::Bool; }

    double number() const;
    bool boolean() const;
    const std::string& string() const;
    const std::vector<Value>& array() const;
    const std::vector<std::pair<std::string, Value>>& object() const;

    // Object member lookup. find() returns nullptr if absent; at() throws.
    const Value* find(const std::string& key) const;
    const Value& at(const std::string& key) const;

    static Value makeNull() { return Value(); }
    // Values built from data rather than parsed (the packed bakes, plugin/PackedBake.cpp).
    static Value makeBool(bool b);
    static Value makeNumber(double x);
    static Value makeString(std::string s);
    static Value makeArray(std::vector<Value> items);
    static Value makeObject(std::vector<std::pair<std::string, Value>> members);

private:
    friend class Parser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

Value parse(const std::string& text);
Value parseFile(const std::string& path);

// Structural equality; object member order is ignored, numbers compare with ==.
bool equal(const Value& a, const Value& b);

// Compact one-line rendering (for messages).
std::string toString(const Value& v);

} // namespace cymbalis::json
