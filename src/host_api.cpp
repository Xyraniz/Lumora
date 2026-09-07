#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <string>
#include <cstdio>
#include <cstdlib>
#include <array>

namespace
{
std::string g_clipboard;

bool writeSystemClipboard(const std::string& text)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return false;
    const std::array<const char*, 3> commands = {{"timeout 1s wl-copy 2>/dev/null", "timeout 1s xclip -selection clipboard 2>/dev/null", "timeout 1s xsel --clipboard --input 2>/dev/null"}};
    for (const char* command : commands)
    {
        FILE* pipe = popen(command, "w");
        if (!pipe) continue;
        const size_t written = fwrite(text.data(), 1, text.size(), pipe);
        const int status = pclose(pipe);
        if (written == text.size() && status == 0) return true;
    }
    return false;
}

bool readSystemClipboard(std::string& output)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return false;
    const std::array<const char*, 3> commands = {{"timeout 1s wl-paste --no-newline 2>/dev/null", "timeout 1s xclip -selection clipboard -o 2>/dev/null", "timeout 1s xsel --clipboard --output 2>/dev/null"}};
    for (const char* command : commands)
    {
        FILE* pipe = popen(command, "r");
        if (!pipe) continue;
        std::string value;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) value += buffer;
        const int status = pclose(pipe);
        if (status == 0)
        {
            output = std::move(value);
            return true;
        }
    }
    return false;
}

int setClipboard(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    g_clipboard = text ? text : "";
    writeSystemClipboard(g_clipboard);
    return 0;
}

int getClipboard(lua_State* L)
{
    std::string value;
    if (readSystemClipboard(value)) g_clipboard = std::move(value);
    lua_pushlstring(L, g_clipboard.data(), g_clipboard.size());
    return 1;
}

void setStringField(lua_State* L, int table, const char* key, const char* value)
{
    lua_pushstring(L, value);
    lua_setfield(L, table, key);
}

void setBooleanField(lua_State* L, int table, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, table, key);
}

int capabilities(lua_State* L)
{
    lua_newtable(L);
    const int table = lua_gettop(L);
    setStringField(L, table, "runtime", "Lumora");
    setStringField(L, table, "version", "0.3.0");
    std::string ignored;
    const bool systemClipboard = readSystemClipboard(ignored);
    setStringField(L, table, "clipboard", "memory");
    setBooleanField(L, table, "systemClipboardAvailable", systemClipboard);
    setStringField(L, table, "filesystem", "memory");
    setStringField(L, table, "http", "stub");
    setStringField(L, table, "rendering", "headless");
    setStringField(L, table, "network", "disabled");
    setBooleanField(L, table, "debug", true);
    setBooleanField(L, table, "executorHooks", false);
    setBooleanField(L, table, "robloxClient", false);
    return 1;
}

int getCallStack(lua_State* L)
{
    const int firstLevel = int(luaL_optinteger(L, 1, 1));
    const int maxFrames = int(luaL_optinteger(L, 2, 64));
    if (firstLevel < 0)
        luaL_argerror(L, 1, "level must be non-negative");
    if (maxFrames < 0)
        luaL_argerror(L, 2, "maxFrames must be non-negative");

    lua_newtable(L);
    int resultIndex = lua_gettop(L);
    int count = 0;
    for (int level = firstLevel; count < maxFrames; ++level)
    {
        lua_Debug ar{};
        if (!lua_getinfo(L, level, "sln", &ar))
            break;

        lua_newtable(L);
        const int frameIndex = lua_gettop(L);
        lua_pushinteger(L, level);
        lua_setfield(L, frameIndex, "level");
        lua_pushstring(L, ar.name ? ar.name : "");
        lua_setfield(L, frameIndex, "name");
        lua_pushstring(L, ar.what ? ar.what : "");
        lua_setfield(L, frameIndex, "what");
        lua_pushstring(L, ar.source ? ar.source : "");
        lua_setfield(L, frameIndex, "source");
        lua_pushstring(L, ar.short_src ? ar.short_src : "");
        lua_setfield(L, frameIndex, "short_src");
        lua_pushinteger(L, ar.currentline);
        lua_setfield(L, frameIndex, "line");
        lua_pushinteger(L, ar.linedefined);
        lua_setfield(L, frameIndex, "linedefined");
        lua_rawseti(L, resultIndex, ++count);
    }
    return 1;
}

void registerFunction(lua_State* L, int table, const char* name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn, name);
    lua_setfield(L, table, name);
}
}

void registerHostGlobals(lua_State* L)
{
    registerJsonGlobals(L);
    registerFunction(L, LUA_GLOBALSINDEX, "setclipboard", setClipboard);
    registerFunction(L, LUA_GLOBALSINDEX, "getclipboard", getClipboard);
    registerFunction(L, LUA_GLOBALSINDEX, "getcallstack", getCallStack);

    lua_getglobal(L, "lumora");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    const int api = lua_gettop(L);
    setStringField(L, api, "version", "0.3.0");
    registerFunction(L, api, "setClipboard", setClipboard);
    registerFunction(L, api, "getClipboard", getClipboard);
    registerFunction(L, api, "getCallStack", getCallStack);
    registerFunction(L, api, "capabilities", capabilities);
    lua_setglobal(L, "lumora");
}
