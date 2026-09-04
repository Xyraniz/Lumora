#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <string>

namespace
{
std::string g_clipboard;

int setClipboard(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    g_clipboard = text ? text : "";
    return 0;
}

int getClipboard(lua_State* L)
{
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
    setStringField(L, table, "clipboard", "memory");
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

    lua_newtable(L);
    const int api = lua_gettop(L);
    setStringField(L, api, "version", "0.3.0");
    registerFunction(L, api, "setClipboard", setClipboard);
    registerFunction(L, api, "getClipboard", getClipboard);
    registerFunction(L, api, "getCallStack", getCallStack);
    registerFunction(L, api, "capabilities", capabilities);
    lua_setglobal(L, "lumora");
}
