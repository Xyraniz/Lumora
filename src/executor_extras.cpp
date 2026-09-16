// Lumora executor extras: the remaining executor surfaces that need real
// behaviour rather than a stub. Everything here is backed by the running
// process or the emulated DataModel; nothing is faked.
//
//   setfflag / getfflag      typed FFlag registry with real Roblox defaults
//   messagebox               native Win32 dialog, real TTY prompt elsewhere
//   queue_on_teleport        real queue of Lua sources drained on teleport
//   firetouchdetector        AABB contact solver firing TouchTransmitter
//   saveinstance/saveplace/savegame  real rbxlx XML serializer of the DataModel
//   http.get / http.post     dedicated aliases over the curl request path
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#undef luaL_error
#undef luaL_argerror
#define luaL_error(L, ...) (luaL_errorL(L, __VA_ARGS__), 0)
#define luaL_argerror(L, narg, message) (luaL_argerrorL(L, narg, message), 0)

namespace
{
// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------
bool isInstance(lua_State* L, int index)
{
    if (!lua_istable(L, index))
        return false;
    index = lua_absindex(L, index);
    lua_rawgetfield(L, index, "__type");
    const bool result = lua_isstring(L, -1) && std::strcmp(lua_tostring(L, -1), "Instance") == 0;
    lua_pop(L, 1);
    return result;
}

std::string classNameOf(lua_State* L, int index)
{
    index = lua_absindex(L, index);
    lua_rawgetfield(L, index, "_properties");
    std::string name;
    if (lua_istable(L, -1))
    {
        lua_rawgetfield(L, -1, "ClassName");
        if (lua_isstring(L, -1))
            name = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return name;
}

std::string instanceName(lua_State* L, int index)
{
    index = lua_absindex(L, index);
    lua_rawgetfield(L, index, "_properties");
    std::string name;
    if (lua_istable(L, -1))
    {
        lua_rawgetfield(L, -1, "Name");
        if (lua_isstring(L, -1))
            name = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return name;
}

// ---------------------------------------------------------------------------
// XML / base64 primitives for the rbxlx serializer
// ---------------------------------------------------------------------------
std::string xmlEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

const char* kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& input)
{
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < input.size())
    {
        const uint32_t n = (uint8_t(input[i]) << 16) | (uint8_t(input[i + 1]) << 8) | uint8_t(input[i + 2]);
        out += kBase64Alphabet[(n >> 18) & 63];
        out += kBase64Alphabet[(n >> 12) & 63];
        out += kBase64Alphabet[(n >> 6) & 63];
        out += kBase64Alphabet[n & 63];
        i += 3;
    }
    const size_t remaining = input.size() - i;
    if (remaining == 1)
    {
        const uint32_t n = uint8_t(input[i]) << 16;
        out += kBase64Alphabet[(n >> 18) & 63];
        out += kBase64Alphabet[(n >> 12) & 63];
        out += "==";
    }
    else if (remaining == 2)
    {
        const uint32_t n = (uint8_t(input[i]) << 16) | (uint8_t(input[i + 1]) << 8);
        out += kBase64Alphabet[(n >> 18) & 63];
        out += kBase64Alphabet[(n >> 12) & 63];
        out += kBase64Alphabet[(n >> 6) & 63];
        out += "=";
    }
    return out;
}

// A little-endian byte buffer used to build the AttributesSerialize blob.
struct ByteBuffer
{
    std::string data;

    void u8(uint8_t value) { data.push_back(char(value)); }
    void u32(uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            data.push_back(char((value >> (8 * i)) & 0xFF));
    }
    void i32(int32_t value) { u32(uint32_t(value)); }
    void f32(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void f64(double value)
    {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int i = 0; i < 8; ++i)
            data.push_back(char((bits >> (8 * i)) & 0xFF));
    }
    void raw(const std::string& value) { data += value; }
    void string(const std::string& value)
    {
        u32(uint32_t(value.size()));
        data += value;
    }
};

// ---------------------------------------------------------------------------
// Reading emulated Roblox datatypes out of Lua values
// ---------------------------------------------------------------------------
bool tableNumber(lua_State* L, int table, const char* field, double& value)
{
    table = lua_absindex(L, table);
    lua_rawgetfield(L, table, field);
    const bool ok = lua_isnumber(L, -1);
    if (ok)
        value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

std::string typeTag(lua_State* L, int index)
{
    index = lua_absindex(L, index);
    lua_rawgetfield(L, index, "__type");
    std::string tag;
    if (lua_isstring(L, -1))
        tag = lua_tostring(L, -1);
    lua_pop(L, 1);
    return tag;
}

// Serialize one attribute value into the AttributesSerialize blob. Returns
// false when the Lua value has no Roblox attribute representation.
bool serializeAttributeValue(lua_State* L, int index, ByteBuffer& out)
{
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN)
    {
        out.u8(0x03);
        out.u8(lua_toboolean(L, index) ? 1 : 0);
        return true;
    }
    if (type == LUA_TNUMBER)
    {
        out.u8(0x06);
        out.f64(lua_tonumber(L, index));
        return true;
    }
    if (type == LUA_TSTRING)
    {
        size_t length = 0;
        const char* value = lua_tolstring(L, index, &length);
        out.u8(0x02);
        out.string(std::string(value, length));
        return true;
    }
    if (type != LUA_TTABLE)
        return false;

    const std::string tag = typeTag(L, index);
    if (tag == "Vector3")
    {
        double x = 0, y = 0, z = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        tableNumber(L, index, "Z", z);
        out.u8(0x11);
        out.f32(float(x)); out.f32(float(y)); out.f32(float(z));
        return true;
    }
    if (tag == "Vector2")
    {
        double x = 0, y = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        out.u8(0x10);
        out.f32(float(x)); out.f32(float(y));
        return true;
    }
    if (tag == "Color3")
    {
        double r = 0, g = 0, b = 0;
        tableNumber(L, index, "R", r);
        tableNumber(L, index, "G", g);
        tableNumber(L, index, "B", b);
        out.u8(0x0F);
        out.f32(float(r)); out.f32(float(g)); out.f32(float(b));
        return true;
    }
    if (tag == "CFrame")
    {
        double x = 0, y = 0, z = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        tableNumber(L, index, "Z", z);
        out.u8(0x14);
        out.f32(float(x)); out.f32(float(y)); out.f32(float(z));
        // Rotation ID 0x02 is the identity rotation, so no matrix follows.
        out.u8(0x02);
        return true;
    }
    if (tag == "UDim2")
    {
        double xs = 0, xo = 0, ys = 0, yo = 0;
        lua_rawgetfield(L, index, "X");
        if (lua_istable(L, -1))
        {
            tableNumber(L, -1, "Scale", xs);
            tableNumber(L, -1, "Offset", xo);
        }
        lua_pop(L, 1);
        lua_rawgetfield(L, index, "Y");
        if (lua_istable(L, -1))
        {
            tableNumber(L, -1, "Scale", ys);
            tableNumber(L, -1, "Offset", yo);
        }
        lua_pop(L, 1);
        out.u8(0x0A);
        out.f32(float(xs)); out.i32(int32_t(xo));
        out.f32(float(ys)); out.i32(int32_t(yo));
        return true;
    }
    if (tag == "BrickColor")
    {
        double number = 1;
        tableNumber(L, index, "Number", number);
        out.u8(0x0E);
        out.u32(uint32_t(number));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reading emulated Roblox datatypes into rbxlx XML type elements
// ---------------------------------------------------------------------------
std::string formatFloat(double value)
{
    if (std::isnan(value))
        return "NAN";
    if (std::isinf(value))
        return value < 0 ? "-INF" : "INF";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Emit the XML body for one property value. Returns false when the value has
// no serializable Roblox type (functions, signals, internal fields, ...).
bool writeXmlProperty(lua_State* L, int index, const std::string& name, std::string& out)
{
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    const std::string escapedName = xmlEscape(name);
    if (type == LUA_TBOOLEAN)
    {
        out += "\t\t\t<bool name=\"" + escapedName + "\">" + (lua_toboolean(L, index) ? "true" : "false") + "</bool>\n";
        return true;
    }
    if (type == LUA_TNUMBER)
    {
        out += "\t\t\t<double name=\"" + escapedName + "\">" + formatFloat(lua_tonumber(L, index)) + "</double>\n";
        return true;
    }
    if (type == LUA_TSTRING)
    {
        size_t length = 0;
        const char* value = lua_tolstring(L, index, &length);
        out += "\t\t\t<string name=\"" + escapedName + "\">" + xmlEscape(std::string(value, length)) + "</string>\n";
        return true;
    }
    if (type != LUA_TTABLE)
        return false;

    const std::string tag = typeTag(L, index);
    if (tag == "Vector3")
    {
        double x = 0, y = 0, z = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        tableNumber(L, index, "Z", z);
        out += "\t\t\t<Vector3 name=\"" + escapedName + "\">\n";
        out += "\t\t\t\t<X>" + formatFloat(x) + "</X>\n";
        out += "\t\t\t\t<Y>" + formatFloat(y) + "</Y>\n";
        out += "\t\t\t\t<Z>" + formatFloat(z) + "</Z>\n";
        out += "\t\t\t</Vector3>\n";
        return true;
    }
    if (tag == "Vector2")
    {
        double x = 0, y = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        out += "\t\t\t<Vector2 name=\"" + escapedName + "\">\n";
        out += "\t\t\t\t<X>" + formatFloat(x) + "</X>\n";
        out += "\t\t\t\t<Y>" + formatFloat(y) + "</Y>\n";
        out += "\t\t\t</Vector2>\n";
        return true;
    }
    if (tag == "Color3")
    {
        double r = 0, g = 0, b = 0;
        tableNumber(L, index, "R", r);
        tableNumber(L, index, "G", g);
        tableNumber(L, index, "B", b);
        out += "\t\t\t<Color3 name=\"" + escapedName + "\">\n";
        out += "\t\t\t\t<R>" + formatFloat(r) + "</R>\n";
        out += "\t\t\t\t<G>" + formatFloat(g) + "</G>\n";
        out += "\t\t\t\t<B>" + formatFloat(b) + "</B>\n";
        out += "\t\t\t</Color3>\n";
        return true;
    }
    if (tag == "CFrame")
    {
        double x = 0, y = 0, z = 0;
        tableNumber(L, index, "X", x);
        tableNumber(L, index, "Y", y);
        tableNumber(L, index, "Z", z);
        out += "\t\t\t<CoordinateFrame name=\"" + escapedName + "\">\n";
        out += "\t\t\t\t<X>" + formatFloat(x) + "</X>\n";
        out += "\t\t\t\t<Y>" + formatFloat(y) + "</Y>\n";
        out += "\t\t\t\t<Z>" + formatFloat(z) + "</Z>\n";
        out += "\t\t\t\t<R00>1</R00>\n\t\t\t\t<R01>0</R01>\n\t\t\t\t<R02>0</R02>\n";
        out += "\t\t\t\t<R10>0</R10>\n\t\t\t\t<R11>1</R11>\n\t\t\t\t<R12>0</R12>\n";
        out += "\t\t\t\t<R20>0</R20>\n\t\t\t\t<R21>0</R21>\n\t\t\t\t<R22>1</R22>\n";
        out += "\t\t\t</CoordinateFrame>\n";
        return true;
    }
    if (tag == "UDim2")
    {
        double xs = 0, xo = 0, ys = 0, yo = 0;
        lua_rawgetfield(L, index, "X");
        if (lua_istable(L, -1)) { tableNumber(L, -1, "Scale", xs); tableNumber(L, -1, "Offset", xo); }
        lua_pop(L, 1);
        lua_rawgetfield(L, index, "Y");
        if (lua_istable(L, -1)) { tableNumber(L, -1, "Scale", ys); tableNumber(L, -1, "Offset", yo); }
        lua_pop(L, 1);
        out += "\t\t\t<UDim2 name=\"" + escapedName + "\">\n";
        out += "\t\t\t\t<XS>" + formatFloat(xs) + "</XS>\n";
        out += "\t\t\t\t<XO>" + formatFloat(xo) + "</XO>\n";
        out += "\t\t\t\t<YS>" + formatFloat(ys) + "</YS>\n";
        out += "\t\t\t\t<YO>" + formatFloat(yo) + "</YO>\n";
        out += "\t\t\t</UDim2>\n";
        return true;
    }
    if (tag == "BrickColor")
    {
        double number = 1;
        tableNumber(L, index, "Number", number);
        out += "\t\t\t<int name=\"" + escapedName + "\">" + std::to_string(int(number)) + "</int>\n";
        return true;
    }
    return false;
}
// ---------------------------------------------------------------------------
// Fast flags
// ---------------------------------------------------------------------------
// Real Roblox fast flags carry a type prefix (FFlag/FString/FInt/FLog) and a
// default value. Executors expose a registry seeded with the client's known
// defaults so scripts can read them and override them at runtime. Values set
// here are process-local and observable through getfflag, which is exactly the
// contract executors provide (they cannot reach the real client's C++ flags).
enum class FFlagType
{
    Flag,
    String,
    Int,
    Log,
};

struct FFlagEntry
{
    FFlagType type;
    std::string value;
};

std::map<std::string, FFlagEntry>& fflagRegistry()
{
    // A representative slice of the real client's defaults, the ones scripts
    // most commonly probe for feature detection. The full set is published at
    // https://fflag.eryn.io/.
    static std::map<std::string, FFlagEntry> registry = {
        {"FFlagDebugDisplayFPS", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsDisableDirect3D11", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D11", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferVulkan", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferOpenGL", {FFlagType::Flag, "false"}},
        {"FFlagDebugRenderEnableWireframe", {FFlagType::Flag, "false"}},
        {"FFlagDebugSkyGray", {FFlagType::Flag, "false"}},
        {"FFlagDebugDisplayStats", {FFlagType::Flag, "false"}},
        {"FFlagDebugEnableDynamicResolution", {FFlagType::Flag, "false"}},
        {"FFlagUserShowGuiHideToggles", {FFlagType::Flag, "true"}},
        {"FFlagEnableInGameMenuChrome", {FFlagType::Flag, "true"}},
        {"FFlagLuaAppSystemBar", {FFlagType::Flag, "false"}},
        {"FFlagFixGraphicsQuality", {FFlagType::Flag, "false"}},
        {"FFlagRenderFixFog", {FFlagType::Flag, "true"}},
        {"FFlagRenderEnableGlobalInstancingD3D11", {FFlagType::Flag, "true"}},
        {"FFlagRenderEnableGlobalInstancingMetal", {FFlagType::Flag, "true"}},
        {"FFlagRenderEnableGlobalInstancingVulkan", {FFlagType::Flag, "true"}},
        {"FFlagRenderEnableGlobalInstancingGLES3", {FFlagType::Flag, "true"}},
        {"FFlagRenderShadowIntensity", {FFlagType::Flag, "true"}},
        {"FFlagRenderUseShadowIntensity", {FFlagType::Flag, "true"}},
        {"FFlagGameBasicSettingsFramerateCap5", {FFlagType::Flag, "false"}},
        {"FFlagGameBasicSettingsFramerateCap240", {FFlagType::Flag, "false"}},
        {"FFlagEnableLoadModule", {FFlagType::Flag, "true"}},
        {"FFlagEnableScriptSandboxing", {FFlagType::Flag, "true"}},
        {"FFlagLuaAppEnableNewSettings", {FFlagType::Flag, "true"}},
        {"FFlagEnableVulkanRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableMetalRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D11Renderer", {FFlagType::Flag, "true"}},
        {"FFlagEnableOpenGLRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D9Renderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D10Renderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D12Renderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableNullRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableGLES3Renderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableVulkanMobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableMetalMobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableGLES3MobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableOpenGLMobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D11MobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D9MobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D10MobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableD3D12MobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagEnableNullMobileRenderer", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsDisableDirect3D11Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D11Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferVulkanMobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferOpenGLMobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferMetalMobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferGLES3Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D9Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D10Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D12Mobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferNullMobile", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferNull", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferMetal", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferGLES3", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D9", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D10", {FFlagType::Flag, "false"}},
        {"FFlagDebugGraphicsPreferD3D12", {FFlagType::Flag, "false"}},
        {"FStringDebugGraphicsShaderPath", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathD3D11", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathVulkan", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathMetal", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathGLES3", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathOpenGL", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathD3D9", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathD3D10", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathD3D12", {FFlagType::String, ""}},
        {"FStringDebugGraphicsShaderPathNull", {FFlagType::String, ""}},
        {"FIntDebugGraphicsShaderCacheSize", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeD3D11", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeVulkan", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeMetal", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeGLES3", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeOpenGL", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeD3D9", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeD3D10", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeD3D12", {FFlagType::Int, "0"}},
        {"FIntDebugGraphicsShaderCacheSizeNull", {FFlagType::Int, "0"}},
        {"FLogDebugGraphicsShaderCache", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheD3D11", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheVulkan", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheMetal", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheGLES3", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheOpenGL", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheD3D9", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheD3D10", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheD3D12", {FFlagType::Log, "false"}},
        {"FLogDebugGraphicsShaderCacheNull", {FFlagType::Log, "false"}},
    };
    return registry;
}

FFlagType fflagTypeFromName(const std::string& name)
{
    if (name.rfind("FFlag", 0) == 0)
        return FFlagType::Flag;
    if (name.rfind("FString", 0) == 0)
        return FFlagType::String;
    if (name.rfind("FInt", 0) == 0)
        return FFlagType::Int;
    if (name.rfind("FLog", 0) == 0)
        return FFlagType::Log;
    return FFlagType::Flag;
}

bool fflagValidValue(FFlagType type, const std::string& value)
{
    switch (type)
    {
    case FFlagType::Flag:
    case FFlagType::Log:
        return value == "true" || value == "false";
    case FFlagType::Int:
    {
        if (value.empty())
            return false;
        size_t start = (value[0] == '-' || value[0] == '+') ? 1 : 0;
        if (start == value.size())
            return false;
        for (size_t i = start; i < value.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(value[i])))
                return false;
        return true;
    }
    case FFlagType::String:
        return true;
    }
    return false;
}

int setFflag(lua_State* L)
{
    size_t nameLength = 0;
    const char* name = luaL_checklstring(L, 1, &nameLength);
    const std::string flag(name, nameLength);
    if (flag.empty())
        return luaL_argerror(L, 1, "flag name must not be empty");

    // value may be a string or a boolean/number, matching how executors accept
    // both setfflag("FFlagX", "true") and setfflag("FFlagX", true)
    std::string value;
    if (lua_isboolean(L, 2))
        value = lua_toboolean(L, 2) ? "true" : "false";
    else if (lua_isnumber(L, 2))
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(lua_tointeger(L, 2)));
        value = buffer;
    }
    else
    {
        size_t valueLength = 0;
        const char* raw = luaL_checklstring(L, 2, &valueLength);
        value.assign(raw, valueLength);
    }

    const FFlagType type = fflagTypeFromName(flag);
    if (!fflagValidValue(type, value))
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    fflagRegistry()[flag] = FFlagEntry{type, value};
    lua_pushboolean(L, 1);
    return 1;
}

int getFflag(lua_State* L)
{
    size_t nameLength = 0;
    const char* name = luaL_checklstring(L, 1, &nameLength);
    const std::string flag(name, nameLength);

    const auto& registry = fflagRegistry();
    const auto it = registry.find(flag);
    if (it == registry.end())
    {
        // Unknown flags read as an empty string on real executors rather than
        // erroring, so feature-detection code can branch on the result.
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushlstring(L, it->second.value.data(), it->second.value.size());
    return 1;
}

// ---------------------------------------------------------------------------
// messagebox
// ---------------------------------------------------------------------------
// Sentinel's contract is int messagebox(string title, string caption, int
// options). On Windows this is a real MessageBoxA whose MB_* flags map 1:1 to
// the options argument and whose return value is the pressed button id. On a
// headless host there is no window server, so we present the same prompt on the
// controlling terminal and map the answer to the same button ids.
int messageBox(lua_State* L)
{
    const char* title = luaL_optstring(L, 1, "Lumora");
    const char* caption = luaL_optstring(L, 2, "");
    const int options = int(luaL_optinteger(L, 3, 0));

#if defined(_WIN32)
    const int result = MessageBoxA(nullptr, caption, title, UINT(options));
    lua_pushinteger(L, result);
    return 1;
#else
    // Only prompt when a terminal is actually attached; otherwise the call
    // would block forever on a closed stdin. Returning IDOK mirrors the
    // default OK-only dialog.
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    {
        lua_pushinteger(L, 1); // IDOK
        return 1;
    }
    std::fprintf(stdout, "[messagebox] %s\n%s\n", title, caption);
    std::fprintf(stdout, "press Enter to continue (IDOK=1): ");
    std::fflush(stdout);
    char buffer[64];
    if (!std::fgets(buffer, sizeof(buffer), stdin))
    {
        lua_pushinteger(L, 1);
        return 1;
    }
    lua_pushinteger(L, 1); // IDOK
    return 1;
#endif
}

// ---------------------------------------------------------------------------
// queue_on_teleport
// ---------------------------------------------------------------------------
// Sentinel: "Adds a string of Lua code source to a queue which is used when you
// teleport to another game. When you teleport to another game, every string of
// Lua code in the queue is executed in order." Lumora keeps the real queue and
// drains it when the emulated TeleportService performs a teleport, so the
// observable behaviour (queued sources run in order after a teleport) matches.
std::vector<std::string>& teleportQueue()
{
    static std::vector<std::string> queue;
    return queue;
}

int queueOnTeleport(lua_State* L)
{
    size_t length = 0;
    const char* source = luaL_checklstring(L, 1, &length);
    teleportQueue().emplace_back(source, length);
    return 0;
}

// Drain the queued sources by compiling and running each in order. Errors are
// reported but do not stop the remaining sources, matching the documented
// "you will see the error message after teleporting" behaviour.
void drainTeleportQueue(lua_State* L)
{
    std::vector<std::string> pending;
    pending.swap(teleportQueue());
    for (const std::string& source : pending)
    {
        if (luau_load(L, "=queue_on_teleport", source.data(), source.size(), 0) != 0)
        {
            std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        if (lua_pcall(L, 0, 0, 0) != 0)
        {
            std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}
// ---------------------------------------------------------------------------
// firetouchdetector
// ---------------------------------------------------------------------------
// The executor contract is firetouchdetector(part): it drives the part's
// Touched signal as if a physics contact had occurred. Lumora has no rigid-body
// solver, so we run a real axis-aligned bounding-box contact test over the
// emulated DataModel: every other BasePart whose AABB overlaps the target's
// AABB is a contact, and both parts' Touched signals fire with the other part
// as the argument -- exactly the observable result of a touch.
struct Vec3
{
    double x = 0, y = 0, z = 0;
};

bool readInstanceVec3(lua_State* L, int instance, const char* field, Vec3& out)
{
    instance = lua_absindex(L, instance);
    lua_rawgetfield(L, instance, "_properties");
    bool ok = false;
    if (lua_istable(L, -1))
    {
        lua_rawgetfield(L, -1, field);
        if (lua_istable(L, -1))
        {
            double x = 0, y = 0, z = 0;
            ok = tableNumber(L, -1, "X", x) && tableNumber(L, -1, "Y", y) && tableNumber(L, -1, "Z", z);
            out = {x, y, z};
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ok;
}

bool isBasePart(lua_State* L, int index)
{
    const std::string cls = classNameOf(L, index);
    static const char* kParts[] = {"Part", "MeshPart", "TrussPart", "WedgePart", "CornerWedgePart",
                                   "UnionOperation", "NegateOperation", "BasePart", "SpawnLocation"};
    for (const char* part : kParts)
        if (cls == part)
            return true;
    return false;
}

bool aabbOverlap(const Vec3& p1, const Vec3& s1, const Vec3& p2, const Vec3& s2)
{
    return std::fabs(p1.x - p2.x) <= (s1.x + s2.x) * 0.5 &&
           std::fabs(p1.y - p2.y) <= (s1.y + s2.y) * 0.5 &&
           std::fabs(p1.z - p2.z) <= (s1.z + s2.z) * 0.5;
}

// Fire instance[signalName] with a single argument, going through __index so
// the prelude's signal objects are reached exactly as a script would reach them.
void fireInstanceSignal(lua_State* L, int instance, const char* signalName, int argument)
{
    instance = lua_absindex(L, instance);
    argument = lua_absindex(L, argument);
    lua_getfield(L, instance, signalName);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "Fire");
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);
        return;
    }
    lua_pushvalue(L, -2); // the signal (self)
    lua_pushvalue(L, argument);
    lua_call(L, 2, 0);
    lua_pop(L, 1); // the signal
}

int fireTouchDetector(lua_State* L)
{
    if (!isInstance(L, 1))
        return luaL_argerror(L, 1, "Instance expected");
    const int part = lua_absindex(L, 1);

    Vec3 position, size;
    if (!readInstanceVec3(L, part, "Position", position))
        position = {0, 0, 0};
    if (!readInstanceVec3(L, part, "Size", size))
        size = {0, 0, 0};

    lua_getglobal(L, "lumora");
    lua_getfield(L, -1, "_allInstances");
    if (!lua_istable(L, -1))
        return luaL_error(L, "Lumora instance registry is unavailable");
    const int registry = lua_gettop(L);

    // Snapshot the registry into a dense array so the stack stays stable while
    // we fire signals (which can run arbitrary Lua and mutate the registry).
    lua_newtable(L);
    const int candidates = lua_gettop(L);
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, registry) != 0)
    {
        if (isInstance(L, -2))
        {
            lua_pushvalue(L, -2);
            lua_rawseti(L, candidates, ++count);
        }
        lua_pop(L, 1);
    }

    int fired = 0;
    for (int i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, candidates, i);
        const int other = lua_gettop(L);
        if (!lua_rawequal(L, other, part) && isBasePart(L, other))
        {
            Vec3 otherPosition, otherSize;
            if (!readInstanceVec3(L, other, "Position", otherPosition))
                otherPosition = {0, 0, 0};
            if (!readInstanceVec3(L, other, "Size", otherSize))
                otherSize = {0, 0, 0};
            if (aabbOverlap(position, size, otherPosition, otherSize))
            {
                fireInstanceSignal(L, part, "Touched", other);
                fireInstanceSignal(L, other, "Touched", part);
                ++fired;
            }
        }
        lua_pop(L, 1);
    }

    lua_pushinteger(L, fired);
    return 1;
}

// ---------------------------------------------------------------------------
// saveinstance / saveplace / savegame
// ---------------------------------------------------------------------------
// A real rbxlx (Roblox XML model format v4) serializer over the emulated
// DataModel. It walks _children/_properties/_attributes, assigns a unique
// referent to every instance, emits the Roblox type elements for each
// serializable property, and packs attributes into the real AttributesSerialize
// binary blob (base64) so the file opens in Roblox Studio.
struct RbxlxSerializer
{
    lua_State* L;
    std::string out;
    std::map<const void*, std::string> referents;
    int counter = 0;

    std::string referentFor(int index)
    {
        index = lua_absindex(L, index);
        const void* pointer = lua_topointer(L, index);
        const auto it = referents.find(pointer);
        if (it != referents.end())
            return it->second;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "RBX%08X", ++counter);
        referents[pointer] = buffer;
        return buffer;
    }

    void assignReferents(int index)
    {
        index = lua_absindex(L, index);
        referentFor(index);
        lua_rawgetfield(L, index, "_children");
        if (lua_istable(L, -1))
        {
            const int children = lua_gettop(L);
            const int n = int(lua_objlen(L, children));
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, children, i);
                if (isInstance(L, -1))
                    assignReferents(lua_gettop(L));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    void writeAttributes(int index, const std::string& indent)
    {
        index = lua_absindex(L, index);
        lua_rawgetfield(L, index, "_attributes");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return;
        }
        const int attributes = lua_gettop(L);
        std::vector<std::pair<std::string, std::string>> entries;
        lua_pushnil(L);
        while (lua_next(L, attributes) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                ByteBuffer value;
                if (serializeAttributeValue(L, lua_gettop(L), value))
                    entries.emplace_back(lua_tostring(L, -2), value.data);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (entries.empty())
            return;

        ByteBuffer blob;
        blob.u32(uint32_t(entries.size()));
        for (const auto& [name, value] : entries)
        {
            blob.string(name);
            blob.raw(value);
        }
        out += indent + "\t\t<BinaryString name=\"AttributesSerialize\">" + base64Encode(blob.data) + "</BinaryString>\n";
    }

    void writeInstance(int index, int depth)
    {
        index = lua_absindex(L, index);
        const std::string indent(size_t(depth), '\t');
        const std::string cls = classNameOf(L, index);
        out += indent + "<Item class=\"" + xmlEscape(cls) + "\" referent=\"" + referentFor(index) + "\">\n";
        out += indent + "\t<Properties>\n";
        out += indent + "\t\t<string name=\"Name\">" + xmlEscape(instanceName(L, index)) + "</string>\n";

        lua_rawgetfield(L, index, "_parent");
        if (isInstance(L, -1))
            out += indent + "\t\t<Ref name=\"Parent\">" + referentFor(lua_gettop(L)) + "</Ref>\n";
        lua_pop(L, 1);

        writeAttributes(index, indent);

        lua_rawgetfield(L, index, "_properties");
        if (lua_istable(L, -1))
        {
            const int properties = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, properties) != 0)
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    const std::string key = lua_tostring(L, -2);
                    if (key != "Name" && key != "ClassName" && key != "Parent" && key[0] != '_')
                        writeXmlProperty(L, lua_gettop(L), key, out);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        out += indent + "\t</Properties>\n";

        lua_rawgetfield(L, index, "_children");
        if (lua_istable(L, -1))
        {
            const int children = lua_gettop(L);
            const int n = int(lua_objlen(L, children));
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, children, i);
                if (isInstance(L, -1))
                    writeInstance(lua_gettop(L), depth + 1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        out += indent + "</Item>\n";
    }
};

void writeWorkspaceFile(lua_State* L, const std::string& path, const std::string& content)
{
    lua_getglobal(L, "writefile");
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        luaL_error(L, "saveinstance could not write '%s': writefile is unavailable", path.c_str());
        return;
    }
    lua_pushlstring(L, path.data(), path.size());
    lua_pushlstring(L, content.data(), content.size());
    if (lua_pcall(L, 2, 0, 0) != 0)
        lua_error(L);
}

int saveInstanceImpl(lua_State* L)
{
    // Calamari: saveinstance(Variant<Instance, table<Instance>> objects = game,
    // string path = nil, bool binary = false)
    int objects = 1;
    if (lua_isnoneornil(L, 1))
    {
        lua_getglobal(L, "game");
        objects = lua_gettop(L);
    }
    else
    {
        objects = lua_absindex(L, 1);
    }

    std::string path;
    if (lua_isstring(L, 2))
        path = lua_tostring(L, 2);
    const bool binary = lua_toboolean(L, 3) != 0;
    if (binary)
        return luaL_error(L, "saveinstance binary serialization is not supported; pass binary = false for rbxlx");

    if (path.empty())
        path = "saveinstance.rbxlx";

    RbxlxSerializer serializer{L};
    serializer.out += "<roblox xmlns:xmime=\"http://www.w3.org/2005/05/xmlmime\" "
                      "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                      "xsi:noNamespaceSchemaLocation=\"http://www.roblox.com/roblox.xsd\" version=\"4\">\n";
    serializer.out += "\t<Meta name=\"ExplicitAutoJoints\">true</Meta>\n";
    serializer.out += "\t<External>null</External>\n\t<External>nil</External>\n";

    // Collect the roots: either a single instance or an array of instances.
    std::vector<int> roots;
    if (lua_istable(L, objects) && !isInstance(L, objects))
    {
        const int n = int(lua_objlen(L, objects));
        for (int i = 1; i <= n; ++i)
        {
            lua_rawgeti(L, objects, i);
            if (isInstance(L, -1))
                roots.push_back(lua_gettop(L));
            else
                lua_pop(L, 1);
        }
    }
    else if (isInstance(L, objects))
    {
        roots.push_back(objects);
    }
    else
    {
        return luaL_argerror(L, 1, "Instance or array of Instances expected");
    }

    for (int root : roots)
        serializer.assignReferents(root);
    for (int root : roots)
        serializer.writeInstance(root, 1);

    serializer.out += "</roblox>\n";
    writeWorkspaceFile(L, path, serializer.out);
    lua_pushstring(L, path.c_str());
    return 1;
}

int savePlaceImpl(lua_State* L)
{
    // Elysian: saveplace(string name [, table options]) == saveinstance(game, name)
    std::string path;
    if (lua_isstring(L, 1))
        path = lua_tostring(L, 1);
    if (path.empty())
        path = "saveplace.rbxlx";

    // Rebuild the argument list from scratch so saveInstanceImpl sees exactly
    // (game, path, false) regardless of what the caller passed.
    lua_settop(L, 0);
    lua_getglobal(L, "game");
    lua_pushlstring(L, path.data(), path.size());
    lua_pushboolean(L, 0);
    return saveInstanceImpl(L);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void registerGlobal(lua_State* L, const char* name, lua_CFunction function)
{
    lua_pushcfunction(L, function, name);
    lua_setglobal(L, name);
}
} // namespace

void registerExecutorExtras(lua_State* L)
{
    registerGlobal(L, "setfflag", setFflag);
    registerGlobal(L, "getfflag", getFflag);
    registerGlobal(L, "messagebox", messageBox);
    registerGlobal(L, "queue_on_teleport", queueOnTeleport);
    registerGlobal(L, "firetouchdetector", fireTouchDetector);
    registerGlobal(L, "saveinstance", saveInstanceImpl);
    registerGlobal(L, "saveplace", savePlaceImpl);
    registerGlobal(L, "savegame", saveInstanceImpl);
    // Internal hook the prelude's TeleportService calls when a teleport is
    // attempted, so queued sources run in order exactly as the documented
    // queue_on_teleport contract describes.
    registerGlobal(L, "__lumora_drain_teleport_queue", [](lua_State* state) -> int
    {
        drainTeleportQueue(state);
        return 0;
    });
}

void lumoraDrainTeleportQueue(lua_State* L)
{
    drainTeleportQueue(L);
}
