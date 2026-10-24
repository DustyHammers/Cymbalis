#include "PackedBake.h"

#include "Bake.h"

#include <cstring>
#include <limits>

namespace cymbalis::packed
{

namespace
{
constexpr char kMagic[8] = { 'C', 'Y', 'M', 'B', 'P', 'A', 'C', 'K' };
constexpr juce::uint32 kFormatVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr juce::uint64 kMaxPayload = 512u * 1024u * 1024u; // far above any bake

enum Tag : juce::uint8
{
    tagNull = 0,
    tagFalse = 1,
    tagTrue = 2,
    tagNumber = 3,
    tagString = 4,
    tagArray = 5,
    tagObject = 6,
    tagF64Array = 7, // an array of numbers only
    tagI32Array = 8  // an array of numbers that are all exactly int32
};

juce::uint64 fnv1a64 (const void* data, size_t size)
{
    juce::uint64 h = 0xcbf29ce484222325ULL;
    const auto* p = static_cast<const juce::uint8*> (data);
    for (size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

juce::uint64 bitsOf (double x)
{
    juce::uint64 u;
    std::memcpy (&u, &x, sizeof u);
    return u;
}

bool isExactInt32 (double x)
{
    if (! (x >= (double) std::numeric_limits<juce::int32>::min() && x <= (double) std::numeric_limits<juce::int32>::max()))
        return false;
    return bitsOf ((double) (juce::int32) x) == bitsOf (x); // also keeps -0.0 out
}

bool numbersOnly (const std::vector<json::Value>& items)
{
    for (const auto& v : items)
        if (! v.isNumber())
            return false;
    return ! items.empty();
}

void writeCount (juce::MemoryOutputStream& out, size_t n)
{
    if (n > std::numeric_limits<juce::uint32>::max())
        throw BakeError ("packed bake: too many items");
    out.writeInt ((int) (juce::uint32) n); // little-endian
}

void writeText (juce::MemoryOutputStream& out, const std::string& s)
{
    writeCount (out, s.size());
    out.write (s.data(), s.size());
}

void writeValue (juce::MemoryOutputStream& out, const json::Value& v)
{
    switch (v.type())
    {
        case json::Value::Type::Null: out.writeByte ((char) tagNull); return;
        case json::Value::Type::Bool: out.writeByte ((char) (v.boolean() ? tagTrue : tagFalse)); return;
        case json::Value::Type::Number:
            out.writeByte ((char) tagNumber);
            out.writeDouble (v.number()); // the double's bits, little-endian
            return;
        case json::Value::Type::String:
            out.writeByte ((char) tagString);
            writeText (out, v.string());
            return;
        case json::Value::Type::Array:
        {
            const auto& items = v.array();
            if (numbersOnly (items))
            {
                bool ints = true;
                for (const auto& x : items)
                    ints = ints && isExactInt32 (x.number());
                out.writeByte ((char) (ints ? tagI32Array : tagF64Array));
                writeCount (out, items.size());
                for (const auto& x : items)
                {
                    if (ints)
                        out.writeInt ((int) (juce::int32) x.number());
                    else
                        out.writeDouble (x.number());
                }
                return;
            }
            out.writeByte ((char) tagArray);
            writeCount (out, items.size());
            for (const auto& x : items)
                writeValue (out, x);
            return;
        }
        case json::Value::Type::Object:
            out.writeByte ((char) tagObject);
            writeCount (out, v.object().size());
            for (const auto& [key, x] : v.object())
            {
                writeText (out, key);
                writeValue (out, x);
            }
            return;
    }
}

// Reads the payload, every read checked against its end.
class Reader
{
public:
    Reader (const juce::uint8* data, size_t size, const std::string& n) : p (data), end (data + size), name (n) {}

    json::Value value (int depth)
    {
        if (depth > 64)
            damaged ("nesting too deep");
        const auto tag = byte();
        switch (tag)
        {
            case tagNull: return json::Value::makeNull();
            case tagFalse: return json::Value::makeBool (false);
            case tagTrue: return json::Value::makeBool (true);
            case tagNumber: return json::Value::makeNumber (f64());
            case tagString: return json::Value::makeString (text());
            case tagArray:
            {
                const auto n = count (1);
                std::vector<json::Value> items;
                items.reserve (n);
                for (size_t i = 0; i < n; ++i)
                    items.push_back (value (depth + 1));
                return json::Value::makeArray (std::move (items));
            }
            case tagObject:
            {
                const auto n = count (5);
                std::vector<std::pair<std::string, json::Value>> members;
                members.reserve (n);
                for (size_t i = 0; i < n; ++i)
                {
                    auto key = text();
                    members.emplace_back (std::move (key), value (depth + 1));
                }
                return json::Value::makeObject (std::move (members));
            }
            case tagF64Array:
            case tagI32Array:
            {
                const bool ints = tag == tagI32Array;
                const auto n = count (ints ? 4 : 8);
                std::vector<json::Value> items;
                items.reserve (n);
                for (size_t i = 0; i < n; ++i)
                    items.push_back (json::Value::makeNumber (ints ? (double) i32() : f64()));
                return json::Value::makeArray (std::move (items));
            }
            default: damaged ("unknown tag");
        }
    }

    bool atEnd() const { return p == end; }

    [[noreturn]] void damaged (const char* what) const
    {
        throw BakeError (name + ": the packed preset is damaged (" + what + ")");
    }

private:
    const juce::uint8* need (size_t n)
    {
        if ((size_t) (end - p) < n)
            damaged ("truncated");
        const auto* at = p;
        p += n;
        return at;
    }
    juce::uint8 byte() { return *need (1); }
    juce::uint32 u32() { return juce::ByteOrder::littleEndianInt (need (4)); }
    juce::int32 i32() { return (juce::int32) u32(); }
    double f64()
    {
        const juce::uint64 u = juce::ByteOrder::littleEndianInt64 (need (8));
        double x;
        std::memcpy (&x, &u, sizeof x);
        return x;
    }
    // A count of items at least `minBytes` each: more than the rest of the payload holds
    // means damage, not a huge allocation.
    size_t count (size_t minBytes)
    {
        const size_t n = u32();
        if (n > (size_t) (end - p) / minBytes)
            damaged ("bad count");
        return n;
    }
    std::string text()
    {
        const size_t n = count (1);
        const auto* s = need (n);
        return std::string (reinterpret_cast<const char*> (s), n);
    }

    const juce::uint8* p;
    const juce::uint8* const end;
    const std::string& name;
};
} // namespace

juce::String resourceName (const juce::String& bakeFile)
{
    return bakeFile.upToLastOccurrenceOf (".json", false, false) + ".pack";
}

juce::MemoryBlock pack (const json::Value& root)
{
    juce::MemoryOutputStream payload;
    writeValue (payload, root);

    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream zlib (compressed, 9); // zlib format; finished when it goes
        zlib.write (payload.getData(), payload.getDataSize());
    }

    juce::MemoryOutputStream out;
    out.write (kMagic, sizeof kMagic);
    out.writeInt ((int) kFormatVersion);
    out.writeInt (0);
    out.writeInt64 ((juce::int64) payload.getDataSize());
    out.writeInt64 ((juce::int64) fnv1a64 (payload.getData(), payload.getDataSize()));
    out.write (compressed.getData(), compressed.getDataSize());
    return out.getMemoryBlock();
}

json::Value unpack (const void* data, size_t size, const std::string& name)
{
    const auto* bytes = static_cast<const juce::uint8*> (data);
    if (data == nullptr || size < kHeaderSize || std::memcmp (bytes, kMagic, sizeof kMagic) != 0)
        throw BakeError (name + ": not a packed preset");
    const auto version = juce::ByteOrder::littleEndianInt (bytes + 8);
    if (version != kFormatVersion)
        throw BakeError (name + ": packed preset format " + std::to_string (version) + ", this plugin reads format "
                         + std::to_string (kFormatVersion));
    const auto payloadSize = juce::ByteOrder::littleEndianInt64 (bytes + 16);
    const auto checksum = juce::ByteOrder::littleEndianInt64 (bytes + 24);
    if (payloadSize > kMaxPayload)
        throw BakeError (name + ": the packed preset is damaged (bad size)");

    std::vector<juce::uint8> payload ((size_t) payloadSize);
    {
        juce::MemoryInputStream source (bytes + kHeaderSize, size - kHeaderSize, false);
        juce::GZIPDecompressorInputStream zlib (&source, false, juce::GZIPDecompressorInputStream::zlibFormat,
                                                (juce::int64) payloadSize);
        size_t got = 0;
        while (got < payload.size())
        {
            const int n = zlib.read (payload.data() + got, (int) std::min<size_t> (payload.size() - got, 1 << 20));
            if (n <= 0)
                break;
            got += (size_t) n;
        }
        if (got != payload.size())
            throw BakeError (name + ": the packed preset is damaged (it does not decompress to its size)");
    }
    if (fnv1a64 (payload.data(), payload.size()) != checksum)
        throw BakeError (name + ": the packed preset fails its checksum");

    Reader reader (payload.data(), payload.size(), name);
    auto root = reader.value (0);
    if (! reader.atEnd())
        reader.damaged ("data after the end");
    return root;
}

bool identical (const json::Value& a, const json::Value& b)
{
    if (a.type() != b.type())
        return false;
    switch (a.type())
    {
        case json::Value::Type::Null: return true;
        case json::Value::Type::Bool: return a.boolean() == b.boolean();
        case json::Value::Type::Number: return bitsOf (a.number()) == bitsOf (b.number());
        case json::Value::Type::String: return a.string() == b.string();
        case json::Value::Type::Array:
        {
            const auto& x = a.array();
            const auto& y = b.array();
            if (x.size() != y.size())
                return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (! identical (x[i], y[i]))
                    return false;
            return true;
        }
        case json::Value::Type::Object:
        {
            const auto& x = a.object();
            const auto& y = b.object();
            if (x.size() != y.size())
                return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (x[i].first != y[i].first || ! identical (x[i].second, y[i].second))
                    return false;
            return true;
        }
    }
    return false;
}

} // namespace cymbalis::packed
