#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstring>

std::string readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("cannot open script: ") + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void pushArgs(lua_State* L, int argc, char** argv, int scriptIndex)
{
    lua_createtable(L, argc - scriptIndex, 0);
    for (int i = scriptIndex; i < argc; ++i)
    {
        lua_pushinteger(L, i - scriptIndex);
        lua_pushstring(L, argv[i]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, "arg");
}

static double g_timeoutSeconds = 0.0;
static std::chrono::steady_clock::time_point g_started;

static void timeoutInterrupt(lua_State* L, int gc)
{
    if (gc >= 0 || g_timeoutSeconds <= 0.0)
        return;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_started).count();
    if (elapsed >= g_timeoutSeconds)
        luaL_error(L, "execution timeout after %.3g seconds", g_timeoutSeconds);
}

// Apply sandbox restrictions: remove dangerous globals so untrusted scripts
// cannot compile code dynamically, access the OS, or call executor hooks.
// This is a defense-in-depth layer; Lumora is still NOT a security sandbox
// and untrusted code must additionally run in an isolated container.
void applySandbox(lua_State* L)
{
    const char* kDangerousGlobals[] = {
        "loadstring", "load", "dofile", "loadfile",
        "os", "io",
        "getgenv", "getrenv", "hookfunction", "hookmetamethod",
        "getrawmetatable", "setrawmetatable", "getnamecallmethod",
        "setnamecallmethod", "checkcaller", "cloneref", "clonereference",
        "request", "syn", "Drawing", "writefile", "readfile", "isfile",
        "isfolder", "makefolder", "delfile", "delfolder", "listfiles",
        "appendfile", "getconnections", "gethui", "protectgui", "setclipboard",
        nullptr};
    for (int i = 0; kDangerousGlobals[i]; ++i)
    {
        lua_pushnil(L);
        lua_setglobal(L, kDangerousGlobals[i]);
    }
    // Cap the scheduler so a sandboxed script cannot spawn unbounded threads.
    lua_getglobal(L, "task");
    if (lua_istable(L, -1))
    {
        lua_pushinteger(L, 10);
        lua_setfield(L, -2, "_maxCycles");
    }
    lua_pop(L, 1);
}

int runScript(const char* path, int argc, char** argv, bool roblox, bool sandbox, double timeout)
{
    const std::string source = readFile(path);
    lua_State* L = luaL_newstate();
    if (!L) { std::cerr << "failed to create Luau state\\n"; return 70; }
    luaL_openlibs(L);
    pushArgs(L, argc, argv, 1);
    if (roblox && !installPrelude(L))
    {
        std::cerr << lua_tostring(L, -1) << "\\n";
        lua_close(L); return 70;
    }
    if (roblox)
        registerRobloxGlobals(L);
    if (sandbox)
        applySandbox(L);
    g_timeoutSeconds = timeout;
    g_started = std::chrono::steady_clock::now();
    if (timeout > 0.0) lua_callbacks(L)->interrupt = timeoutInterrupt;
    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    const std::string bytecode = Luau::compile(source, options);
    int rc = 0;
    if (luau_load(L, path, bytecode.data(), bytecode.size(), 0) != 0 || lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
    {
        std::cerr << lua_tostring(L, -1) << "\\n";
        rc = 1;
    }
    // After the main script body runs, resume any spawned coroutines via
    // the task scheduler (defined in the Roblox prelude as task._runScheduler).
    if (roblox && rc == 0)
    {
        lua_getglobal(L, "task");
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "_runScheduler");
            if (lua_isfunction(L, -1))
                lua_pcall(L, 0, 0, 0);
            else
                lua_pop(L, 1);
        }
        else
            lua_pop(L, 1);
    }
    lua_close(L);
    return rc;
}
