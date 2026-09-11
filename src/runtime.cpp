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

// ── Reference-CLI compatibility globals ─────────────────────────────────────
// The official Luau REPL registers these two globals on top of the stock
// base library (CLI/src/Repl.cpp). Lumora previously did not, so any script
// using loadstring()/collectgarbage() errored with "attempt to call a nil
// value" while the same script ran fine under `luau`. Both are registered
// before the Roblox prelude; in Roblox mode the prelude's own loadstring
// (which mirrors Roblox chunk-name semantics) overwrites ours afterwards.

// loadstring(source [, chunkname]) -> fn | (nil, err)
// Compiled with the same optimization/debug levels as the main script.
static int cliLoadstring(lua_State* L)
{
    size_t len = 0;
    const char* source = luaL_checklstring(L, 1, &len);
    const char* chunkname = luaL_optstring(L, 2, source);

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;

    const std::string bytecode = Luau::compile(std::string(source, len), options);
    if (luau_load(L, chunkname, bytecode.data(), bytecode.size(), 0) == 0)
        return 1;

    lua_pushnil(L);
    lua_insert(L, -2); // put nil before the error message
    return 2;          // nil, error
}

// collectgarbage("count"|"collect") — mirrors the official CLI's restricted
// variant (only 'count' and 'collect' are accepted; anything else errors).
static int cliCollectgarbage(lua_State* L)
{
    const char* option = luaL_optstring(L, 1, "collect");
    if (strcmp(option, "collect") == 0)
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
        return 0;
    }
    if (strcmp(option, "count") == 0)
    {
        const int count = lua_gc(L, LUA_GCCOUNT, 0);
        lua_pushnumber(L, count);
        return 1;
    }
    luaL_error(L, "collectgarbage must be called with 'count' or 'collect'");
}

void registerCLIGlobals(lua_State* L)
{
    lua_pushcfunction(L, cliLoadstring, "loadstring");
    lua_setglobal(L, "loadstring");
    lua_pushcfunction(L, cliCollectgarbage, "collectgarbage");
    lua_setglobal(L, "collectgarbage");
}

static void reportLuaError(lua_State* L)
{
    // Match the official CLI's uncaught-error report (CLI/src/Repl.cpp):
    // the message itself, then a "stacktrace:" section rendered by the VM
    // (lua_debugtrace). Anything else (e.g. a literal \n) makes Lumora's
    // stderr visibly different from `luau` for the same failing script.
    std::string error;
    if (const char* str = lua_tostring(L, -1))
        error = str;
    else
        error = "script error";
    if (error.find("stacktrace:") == std::string::npos)
    {
        error += "\nstacktrace:\n";
        error += lua_debugtrace(L);
    }
    std::cerr << error;
    if (error.empty() || error.back() != '\n')
        std::cerr << "\n";
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
        "getclipboard", "getcallstack", "lumora", "require",
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

// Apply library-table freezing: the builtin library tables (string, table,
// math, os, coroutine, debug, utf8, bit32, buffer, vector) and the string
// metatable become read-only, exactly like the official CLI's luaL_sandbox()
// and like real Roblox, where tampering with a builtin library errors with
// "attempt to modify a readonly table".
// Deliberately NOT frozen (Roblox parity):
//   - the globals environment itself: Roblox scripts assign globals freely
//     (`X = 1`, `_G.X = 1`), and Lumora's own prelude installs globals after
//     luaL_openlibs. The official CLI freezes _G because it runs untrusted
//     files, but Lumora targets Roblox semantics where environments are
//     per-script and writable.
//   - task/lumora: mutable runtime state (scheduler bookkeeping, host caps).
//   - arg / any script-provided table: not builtin libraries.
void freezeLibraries(lua_State* L)
{
    static const char* kLibraries[] = {
        "string", "table", "math", "os", "coroutine", "debug",
        "utf8", "bit32", "buffer", "vector", nullptr,
    };
    for (int i = 0; kLibraries[i]; ++i)
    {
        lua_getglobal(L, kLibraries[i]);
        if (lua_istable(L, -1))
            lua_setreadonly(L, -1, true);
        lua_pop(L, 1);
    }
    // Freeze the string metatable as well (luaL_sandbox does this too);
    // scripts must not be able to swap __index of ("")'s metatable.
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1))
    {
        lua_setreadonly(L, -1, true);
        lua_pop(L, 2);
    }
    else
    {
        lua_pop(L, 1);
    }
}

int runScript(const char* path, int argc, char** argv, bool roblox, bool sandbox, double timeout)
{
    const std::string source = readFile(path);
    lua_State* L = luaL_newstate();
    if (!L) { std::cerr << "failed to create Luau state\n"; return 70; }
    luaL_openlibs(L);

    // The official Luau CLI registers loadstring and collectgarbage as
    // globals (CLI/src/Repl.cpp). Luau's stock base library omits them (it
    // only ships gcinfo), so scripts written against the reference CLI
    // crashed with "attempt to call a nil value". Register both here, in
    // --no-roblox and Roblox modes, before the prelude so the prelude's own
    // Roblox-semantics loadstring takes precedence afterwards.
    registerCLIGlobals(L);

    pushArgs(L, argc, argv, 1);
    if (roblox && !installPrelude(L))
    {
        std::cerr << lua_tostring(L, -1) << "\n";
        lua_close(L); return 70;
    }
    if (roblox)
    {
        registerRobloxGlobals(L);
        registerHostGlobals(L);
    }
    registerEmbeddedLuneHost(L);
    // Luau's require-by-string loader is available in both pure Luau and
    // Roblox-prelude mode.  It resolves relative to the current chunk and
    // keeps module results cached for the lifetime of this state.
    registerRequire(L);
    // Freeze library tables + string metatable AFTER the prelude/registry
    // setup above (they mutate library tables while installing) and BEFORE
    // the user script runs, so the script sees the same read-only surface as
    // the official CLI and real Roblox.
    freezeLibraries(L);
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

    // Match the reference CLI chunk naming: "@" + normalizePath(path) makes
    // runtime errors report the path exactly like `luau` does (e.g.
    // "./main.lua:7: x" for a relative path, "/abs/main.lua:7: x" absolute).
    // Previously Lumora passed the raw path, which the VM rendered as
    // [string "path"] in every error message, stack trace and debug.info call
    // — visibly different from official Luau and misleading to anyone reading
    // the output.
    const std::string chunkName = "@" + normalizeChunkPath(path);

    // Run the script in a fresh thread via lua_resume, exactly like the
    // official CLI's runFile(). This makes the main chunk a coroutine, so
    // coroutine.isyieldable() is true at top level and errors carry a
    // "stacktrace:" section — both observable behaviors of the reference
    // runtime that Lumora previously diverged from.
    lua_State* T = lua_newthread(L);
    const int loadStatus = luau_load(T, chunkName.c_str(), bytecode.data(), bytecode.size(), 0);
    if (loadStatus != 0)
    {
        reportLuaError(T);
        rc = 1;
    }
    else
    {
        const int callStatus = lua_resume(T, NULL, 0);
        if (callStatus != 0)
        {
            reportLuaError(T);
            rc = 1;
        }
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
            {
                const int schedulerStatus = lua_pcall(L, 0, 2, 0);
                if (schedulerStatus != 0)
                {
                    reportLuaError(L);
                    rc = 1;
                }
                else
                {
                    const bool schedulerOk = lua_isboolean(L, -2) && lua_toboolean(L, -2);
                    if (!schedulerOk)
                    {
                        lua_rawgeti(L, -1, 1);
                        const char* taskError = lua_tostring(L, -1);
                        std::cerr << "uncaught task error: " << (taskError ? taskError : "unknown error") << "\n";
                        lua_pop(L, 1);
                        rc = 1;
                    }
                    lua_pop(L, 2);
                }
            }
            else
                lua_pop(L, 1);
        }
        else
            lua_pop(L, 1);
    }
    lua_close(L);
    return rc;
}
