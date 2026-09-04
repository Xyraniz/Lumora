#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

std::string jsonEscape(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value)
    {
        if (c == '\\') out += "\\\\";
        else if (c == '\"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += char(c);
    }
    return out + "\"";
}

namespace
{
class JsonParser
{
public:
    JsonParser(lua_State* state, const char* input, size_t length)
        : L(state), text(input, length)
    {
    }

    void parse()
    {
        skipWhitespace();
        parseValue(0);
        skipWhitespace();
        if (position != text.size())
            fail("trailing characters");
    }

private:
    lua_State* L;
    std::string text;
    size_t position = 0;

    [[noreturn]] void fail(const char* message) const
    {
        throw std::runtime_error(message);
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error(message);
    }

    void skipWhitespace()
    {
        while (position < text.size())
        {
            const char c = text[position];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++position;
        }
    }

    bool consume(char expected)
    {
        if (position < text.size() && text[position] == expected)
        {
            ++position;
            return true;
        }
        return false;
    }

    void expect(char expected)
    {
        if (!consume(expected))
        {
            std::string message = "expected '";
            message += expected;
            message += "'";
            fail(message);
        }
    }

    static int hexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void appendUtf8(std::string& output, unsigned codepoint)
    {
        if (codepoint <= 0x7f)
            output.push_back(char(codepoint));
        else if (codepoint <= 0x7ff)
        {
            output.push_back(char(0xc0 | (codepoint >> 6)));
            output.push_back(char(0x80 | (codepoint & 0x3f)));
        }
        else
        {
            output.push_back(char(0xe0 | (codepoint >> 12)));
            output.push_back(char(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(char(0x80 | (codepoint & 0x3f)));
        }
    }

    void parseString()
    {
        expect('"');
        std::string value;
        while (position < text.size())
        {
            const unsigned char c = static_cast<unsigned char>(text[position++]);
            if (c == '"')
            {
                lua_pushlstring(L, value.data(), value.size());
                return;
            }
            if (c < 0x20)
                fail("control character in string");
            if (c != '\\')
            {
                value.push_back(char(c));
                continue;
            }
            if (position >= text.size()) fail("unfinished escape sequence");
            const char escape = text[position++];
            switch (escape)
            {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u':
            {
                if (position + 4 > text.size()) fail("short unicode escape");
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const int digit = hexValue(text[position++]);
                    if (digit < 0) fail("invalid unicode escape");
                    codepoint = (codepoint << 4) | unsigned(digit);
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdfff)
                    fail("surrogate unicode escape is not supported");
                appendUtf8(value, codepoint);
                break;
            }
            default: fail("invalid escape sequence");
            }
        }
        fail("unterminated string");
    }

    void parseNumber()
    {
        const char* begin = text.c_str() + position;
        char* end = nullptr;
        const double number = std::strtod(begin, &end);
        if (end == begin || !std::isfinite(number))
            fail("invalid number");
        position = size_t(end - text.c_str());
        lua_pushnumber(L, number);
    }

    void parseLiteral(const char* literal, int type)
    {
        const size_t length = std::char_traits<char>::length(literal);
        if (text.compare(position, length, literal) != 0)
            fail("invalid value");
        position += length;
        if (type == 1) lua_pushboolean(L, true);
        else if (type == 2) lua_pushboolean(L, false);
        else lua_pushnil(L);
    }

    void parseArray(int depth)
    {
        expect('[');
        lua_newtable(L);
        const int table = lua_gettop(L);
        int index = 1;
        skipWhitespace();
        if (consume(']')) return;
        for (;;)
        {
            skipWhitespace();
            parseValue(depth + 1);
            if (!lua_isnil(L, -1)) lua_rawseti(L, table, index);
            else lua_pop(L, 1);
            ++index;
            skipWhitespace();
            if (consume(']')) return;
            expect(',');
        }
    }

    void parseObject(int depth)
    {
        expect('{');
        lua_newtable(L);
        const int table = lua_gettop(L);
        skipWhitespace();
        if (consume('}')) return;
        for (;;)
        {
            skipWhitespace();
            if (position >= text.size() || text[position] != '"')
                fail("object keys must be strings");
            parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            parseValue(depth + 1);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 2); // value and key
            }
            else
            {
                lua_rawset(L, table);
            }
            skipWhitespace();
            if (consume('}')) return;
            expect(',');
        }
    }

    void parseValue(int depth)
    {
        if (depth > 128) fail("maximum nesting depth exceeded");
        skipWhitespace();
        if (position >= text.size()) fail("unexpected end of input");
        switch (text[position])
        {
        case 'n': parseLiteral("null", 0); return;
        case 't': parseLiteral("true", 1); return;
        case 'f': parseLiteral("false", 2); return;
        case '"': parseString(); return;
        case '[': parseArray(depth); return;
        case '{': parseObject(depth); return;
        default:
            if (text[position] == '-' || (text[position] >= '0' && text[position] <= '9'))
            {
                parseNumber();
                return;
            }
            fail("unexpected character");
        }
    }
};

void appendJsonString(std::string& output, const char* value, size_t length)
{
    output += jsonEscape(std::string(value, length));
}

void appendJsonValue(lua_State* L, int index, std::string& output,
                     std::unordered_set<const void*>& active, int depth)
{
    if (depth > 128) throw std::runtime_error("maximum nesting depth exceeded");
    const int type = lua_type(L, index);
    switch (type)
    {
    case LUA_TNIL:
        output += "null";
        return;
    case LUA_TBOOLEAN:
        output += lua_toboolean(L, index) ? "true" : "false";
        return;
    case LUA_TNUMBER:
    case LUA_TINTEGER:
    {
        const double number = lua_tonumber(L, index);
        if (!std::isfinite(number)) throw std::runtime_error("cannot encode non-finite number");
        std::ostringstream stream;
        stream << std::setprecision(17) << number;
        output += stream.str();
        return;
    }
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* value = lua_tolstring(L, index, &length);
        appendJsonString(output, value ? value : "", length);
        return;
    }
    case LUA_TTABLE:
    {
        const void* identity = lua_topointer(L, index);
        if (!active.insert(identity).second)
            throw std::runtime_error("cannot encode cyclic table");

        const int table = lua_absindex(L, index);
        int length = lua_objlen(L, table);
        bool array = length > 0;
        int keyCount = 0;
        lua_pushnil(L);
        while (lua_next(L, table) != 0)
        {
            const int keyType = lua_type(L, -2);
            bool validArrayKey = (keyType == LUA_TNUMBER || keyType == LUA_TINTEGER) && lua_isnumber(L, -2);
            if (validArrayKey)
            {
                const double numericKey = lua_tonumber(L, -2);
                validArrayKey = std::isfinite(numericKey) && std::floor(numericKey) == numericKey &&
                    numericKey >= 1 && numericKey <= length;
            }
            if (!validArrayKey) array = false;
            ++keyCount;
            lua_pop(L, 1);
        }
        if (array && keyCount == length)
        {
            output.push_back('[');
            for (int i = 1; i <= length; ++i)
            {
                if (i > 1) output.push_back(',');
                lua_rawgeti(L, table, i);
                appendJsonValue(L, -1, output, active, depth + 1);
                lua_pop(L, 1);
            }
            output.push_back(']');
        }
        else
        {
            std::vector<std::string> keys;
            lua_pushnil(L);
            while (lua_next(L, table) != 0)
            {
                if (lua_type(L, -2) != LUA_TSTRING)
                {
                    lua_pop(L, 1);
                    active.erase(identity);
                    throw std::runtime_error("object keys must be strings");
                }
                keys.emplace_back(lua_tostring(L, -2));
                lua_pop(L, 1);
            }
            std::sort(keys.begin(), keys.end());
            output.push_back('{');
            for (size_t i = 0; i < keys.size(); ++i)
            {
                if (i > 0) output.push_back(',');
                output += jsonEscape(keys[i]);
                output.push_back(':');
                lua_getfield(L, table, keys[i].c_str());
                appendJsonValue(L, -1, output, active, depth + 1);
                lua_pop(L, 1);
            }
            output.push_back('}');
        }
        active.erase(identity);
        return;
    }
    default:
    {
        std::string message = "unsupported value type: ";
        message += lua_typename(L, type);
        throw std::runtime_error(message);
    }
    }
}

int jsonEncode(lua_State* L)
{
    const int valueIndex = lua_gettop(L) >= 2 ? 2 : 1;
    luaL_checkany(L, valueIndex);
    try
    {
        std::string output;
        std::unordered_set<const void*> active;
        appendJsonValue(L, valueIndex, output, active, 0);
        lua_pushlstring(L, output.data(), output.size());
        return 1;
    }
    catch (const std::exception& error)
    {
        luaL_error(L, "JSONEncode: %s", error.what());
    }
}

int jsonDecode(lua_State* L)
{
    const int valueIndex = lua_gettop(L) >= 2 ? 2 : 1;
    size_t length = 0;
    const char* input = luaL_checklstring(L, valueIndex, &length);
    try
    {
        JsonParser parser(L, input, length);
        parser.parse();
        return 1;
    }
    catch (const std::exception& error)
    {
        luaL_error(L, "JSONDecode: %s", error.what());
    }
}

void registerJsonFunction(lua_State* L, int table, const char* name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn, name);
    lua_setfield(L, table, name);
}
}

void registerJsonGlobals(lua_State* L)
{
    lua_getglobal(L, "HttpService");
    if (lua_istable(L, -1))
    {
        const int service = lua_absindex(L, -1);
        registerJsonFunction(L, service, "JSONEncode", jsonEncode);
        registerJsonFunction(L, service, "JSONDecode", jsonDecode);
    }
    lua_pop(L, 1);

    lua_newtable(L);
    const int api = lua_gettop(L);
    registerJsonFunction(L, api, "encode", jsonEncode);
    registerJsonFunction(L, api, "decode", jsonDecode);
    lua_setglobal(L, "json");
}
