// ============================================================
// json_mini.h  -  SmartCare Phase 3
// A small, dependency-free JSON parser (recursive descent) used
// to read Firebase REST responses in C++.
//
// Why not a library? nlohmann/json is the standard choice and is
// a fine drop-in replacement. This parser keeps the project
// single-folder and is short enough to explain in a viva.
//
// Works with C++11 or newer (tested with GCC; Dev-C++ TDM-GCC 4.9.2 OK with -std=gnu++11).
// ============================================================
#ifndef SMARTCARE_JSON_MINI_H
#define SMARTCARE_JSON_MINI_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace smartcare {

class JsonValue
{
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type = Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;               // String value
    std::vector<std::string> keys;  // Object keys, parallel to items
    std::vector<JsonValue> items;   // Array elements or Object values

    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    bool isString() const { return type == String; }
    bool isNumber() const { return type == Number; }
    bool isBool() const { return type == Bool; }

    // Linear search is fine here: Firebase records have few fields.
    const JsonValue *find(const std::string &key) const
    {
        if (type != Object)
            return nullptr;
        for (size_t i = 0; i < keys.size(); i++)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }

    // True only for whole numbers that fit in an int (rejects 3.5, 1e12).
    bool asInt(int &out) const
    {
        if (type != Number || std::floor(number) != number ||
            number < -2147483648.0 || number > 2147483647.0)
            return false;
        out = static_cast<int>(number);
        return true;
    }
};

class JsonParser
{
public:
    // Throws std::runtime_error with a position on malformed input.
    static JsonValue parse(const std::string &input)
    {
        JsonParser p(input);
        JsonValue v = p.parseValue(0);
        p.skipWs();
        if (p.pos != p.s.size())
            p.fail("unexpected trailing characters");
        return v;
    }

private:
    static const int MAX_DEPTH = 64; // guards against stack overflow
    const std::string &s;
    size_t pos = 0;

    explicit JsonParser(const std::string &in) : s(in) {}

    [[noreturn]] void fail(const std::string &msg) const
    {
        throw std::runtime_error("JSON parse error at position " +
                                 std::to_string(pos) + ": " + msg);
    }

    void skipWs()
    {
        while (pos < s.size() &&
               (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            ++pos;
    }

    bool consume(char c)
    {
        skipWs();
        if (pos < s.size() && s[pos] == c)
        {
            ++pos;
            return true;
        }
        return false;
    }

    void expect(char c)
    {
        if (!consume(c))
            fail(std::string("expected '") + c + "'");
    }

    bool matchWord(const char *w)
    {
        size_t n = std::strlen(w);
        if (s.compare(pos, n, w) == 0)
        {
            pos += n;
            return true;
        }
        return false;
    }

    JsonValue parseValue(int depth)
    {
        if (depth > MAX_DEPTH)
            fail("nesting too deep");
        skipWs();
        if (pos >= s.size())
            fail("unexpected end of input");

        char c = s[pos];
        JsonValue v;
        if (c == '{')
            return parseObject(depth);
        if (c == '[')
            return parseArray(depth);
        if (c == '"')
        {
            v.type = JsonValue::String;
            v.text = parseString();
            return v;
        }
        if (matchWord("true"))
        {
            v.type = JsonValue::Bool;
            v.boolean = true;
            return v;
        }
        if (matchWord("false"))
        {
            v.type = JsonValue::Bool;
            return v;
        }
        if (matchWord("null"))
            return v;
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();
        fail(std::string("unexpected character '") + c + "'");
    }

    JsonValue parseObject(int depth)
    {
        JsonValue v;
        v.type = JsonValue::Object;
        ++pos; // '{'
        if (consume('}'))
            return v;
        do
        {
            skipWs();
            if (pos >= s.size() || s[pos] != '"')
                fail("expected string key");
            std::string key = parseString();
            expect(':');
            v.keys.push_back(key);
            v.items.push_back(parseValue(depth + 1));
        } while (consume(','));
        expect('}');
        return v;
    }

    JsonValue parseArray(int depth)
    {
        JsonValue v;
        v.type = JsonValue::Array;
        ++pos; // '['
        if (consume(']'))
            return v;
        do
        {
            v.items.push_back(parseValue(depth + 1));
        } while (consume(','));
        expect(']');
        return v;
    }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    JsonValue parseNumber()
    {
        // Strict JSON grammar: -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
        size_t start = pos;
        if (s[pos] == '-')
            ++pos;
        if (pos < s.size() && s[pos] == '0')
            ++pos;
        else if (pos < s.size() && isDigit(s[pos]))
            while (pos < s.size() && isDigit(s[pos]))
                ++pos;
        else
            fail("invalid number");

        if (pos < s.size() && s[pos] == '.')
        {
            ++pos;
            if (pos >= s.size() || !isDigit(s[pos]))
                fail("invalid fraction");
            while (pos < s.size() && isDigit(s[pos]))
                ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E'))
        {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
                ++pos;
            if (pos >= s.size() || !isDigit(s[pos]))
                fail("invalid exponent");
            while (pos < s.size() && isDigit(s[pos]))
                ++pos;
        }
        JsonValue v;
        v.type = JsonValue::Number;
        v.number = std::strtod(s.substr(start, pos - start).c_str(), nullptr);
        return v;
    }

    unsigned parseHex4()
    {
        if (pos + 4 > s.size())
            fail("short \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; i++)
        {
            char c = s[pos++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= unsigned(c - 'A' + 10);
            else
                fail("invalid hex digit");
        }
        return value;
    }

    static void appendUtf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80)
            out += char(cp);
        else if (cp < 0x800)
        {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        else
        {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString()
    {
        ++pos; // opening quote
        std::string out;
        while (true)
        {
            if (pos >= s.size())
                fail("unterminated string");
            char c = s[pos++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                fail("control character in string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos >= s.size())
                fail("unterminated escape");
            char e = s[pos++];
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
                if (cp >= 0xD800 && cp <= 0xDBFF) // surrogate pair
                {
                    if (!(pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u'))
                        fail("unpaired surrogate");
                    pos += 2;
                    unsigned low = parseHex4();
                    if (low < 0xDC00 || low > 0xDFFF)
                        fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    fail("unpaired surrogate");
                appendUtf8(out, cp);
                break;
            }
            default:
                fail("invalid escape");
            }
        }
    }
};

// Escape a string for embedding inside a JSON document.
inline std::string jsonEscape(const std::string &in)
{
    std::string out;
    for (char c : in)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                const char *hex = "0123456789abcdef";
                unsigned char u = static_cast<unsigned char>(c);
                out += "\\u00";
                out += hex[u >> 4];
                out += hex[u & 0x0F];
            }
            else
                out += c;
        }
    }
    return out;
}

} // namespace smartcare

#endif
