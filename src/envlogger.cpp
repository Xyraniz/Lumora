#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
constexpr size_t kMaxLogBytes = 7u * 1024u * 1024u;
constexpr size_t kMaxEvents = 50000;
constexpr size_t kMaxLoggedString = 256u * 1024u;
char kLoggerRegistryKey;

struct EnvLogger
{
    explicit EnvLogger(const std::string& path)
        : output(path, std::ios::binary | std::ios::trunc)
    {
        if (!output)
            throw std::runtime_error("cannot create envlogger output: " + path);
    }

    std::ofstream output;
    size_t events = 0;
    size_t bytes = 0;
    size_t blockedNetwork = 0;
    bool truncated = false;

    void record(lua_State* L, const char* event, const std::string& api,
                int firstArg, int argCount, bool blocked);
};

std::string serializeValue(lua_State* L, int index, int depth,
                           std::unordered_set<const void*>& seen)
{
    const int type = lua_type(L, index);
    switch (type)
    {
    case LUA_TNIL: return "null";
    case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "true" : "false";
    case LUA_TNUMBER:
    {
        const double value = lua_tonumber(L, index);
        if (!std::isfinite(value)) return "null";
        std::ostringstream number;
        number << std::setprecision(17) << value;
        return number.str();
    }
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* value = lua_tolstring(L, index, &length);
        if (!value) return "\"\"";
        if (length <= kMaxLoggedString)
            return jsonEscape(std::string(value, length));
        const size_t preview = kMaxLoggedString / 2;
        return std::string("{\"type\":\"string\",\"length\":") + std::to_string(length) +
            ",\"preview\":" + jsonEscape(std::string(value, preview)) +
            ",\"truncated\":true}";
    }
    case LUA_TTABLE:
    {
        const int absolute = lua_absindex(L, index);
        lua_pushliteral(L, "__type");
        lua_rawget(L, absolute);
        const char* tableType = lua_tostring(L, -1);
        const bool instance = tableType && std::string(tableType) == "Instance";
        lua_pop(L, 1);
        if (instance)
        {
            std::string className;
            std::string objectName;
            lua_pushliteral(L, "_properties"); lua_rawget(L, absolute);
            if (lua_istable(L, -1))
            {
                const int properties = lua_absindex(L, -1);
                lua_pushliteral(L, "ClassName"); lua_rawget(L, properties);
                if (const char* value = lua_tostring(L, -1)) className = value;
                lua_pop(L, 1);
                lua_pushliteral(L, "Name"); lua_rawget(L, properties);
                if (const char* value = lua_tostring(L, -1)) objectName = value;
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return "{\"type\":\"Instance\",\"className\":" + jsonEscape(className) +
                ",\"name\":" + jsonEscape(objectName) + "}";
        }
        const void* identity = lua_topointer(L, absolute);
        if (depth >= 2 || !identity || !seen.insert(identity).second)
            return "{\"type\":\"table\"}";

        std::string result = "{\"type\":\"table\",\"entries\":[";
        int entries = 0;
        bool entryTruncated = false;
        lua_pushnil(L);
        while (lua_next(L, absolute) != 0)
        {
            if (entries >= 16)
            {
                lua_pop(L, 2);
                entryTruncated = true;
                break;
            }
            if (entries++) result += ",";
            result += "[" + serializeValue(L, -2, depth + 1, seen) + "," +
                serializeValue(L, -1, depth + 1, seen) + "]";
            lua_pop(L, 1);
        }
        if (entryTruncated) result += ",{\"truncated\":true}";
        result += "]}";
        seen.erase(identity);
        return result;
    }
    case LUA_TFUNCTION: return "{\"type\":\"function\"}";
    case LUA_TTHREAD: return "{\"type\":\"thread\"}";
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
    case LUA_TVECTOR:
    case LUA_TBUFFER:
    case LUA_TCLASS:
    case LUA_TOBJECT:
        return std::string("{\"type\":") + jsonEscape(lua_typename(L, type)) + "}";
    default:
        return std::string("{\"type\":") + jsonEscape(lua_typename(L, type)) + "}";
    }
}

bool isNetworkApi(const std::string& api)
{
    return api.find("Http") != std::string::npos || api.find("http") != std::string::npos ||
        api.find("request") != std::string::npos || api.find("GetAsync") != std::string::npos ||
        api.find("PostAsync") != std::string::npos;
}

void EnvLogger::record(lua_State* L, const char* event, const std::string& api,
                       int firstArg, int argCount, bool blocked)
{
    if (truncated)
        return;
    if (events >= kMaxEvents)
    {
        truncated = true;
        return;
    }

    std::string line = "{\"event\":" + jsonEscape(event) +
        ",\"api\":" + jsonEscape(api) + ",\"args\":[";
    std::unordered_set<const void*> seen;
    const int lastArg = firstArg + std::min(argCount, 32);
    for (int index = firstArg; index < lastArg; ++index)
    {
        if (index > firstArg) line += ",";
        line += serializeValue(L, index, 0, seen);
    }
    if (argCount > lastArg - firstArg)
    {
        if (lastArg > firstArg) line += ",";
        line += "{\"truncatedArguments\":" + std::to_string(argCount - (lastArg - firstArg)) + "}";
    }
    line += blocked ? "],\"blocked\":true}\n" : "]}\n";
    if (line.size() > kMaxLogBytes - std::min(bytes, kMaxLogBytes))
    {
        truncated = true;
        return;
    }
    output.write(line.data(), std::streamsize(line.size()));
    if (!output)
    {
        truncated = true;
        return;
    }
    bytes += line.size();
    ++events;
    if (blocked && isNetworkApi(api))
        ++blockedNetwork;
}

EnvLogger* context(lua_State* L)
{
    return static_cast<EnvLogger*>(lua_touserdata(L, lua_upvalueindex(3)));
}

int yieldableContinuation(lua_State* L, int)
{
    return lua_gettop(L);
}

int loggedCall(lua_State* L)
{
    EnvLogger* logger = context(L);
    size_t pathLength = 0;
    const char* path = lua_tolstring(L, lua_upvalueindex(2), &pathLength);
    if (logger && path)
        logger->record(L, "call", std::string(path, pathLength), 1, lua_gettop(L), false);
    if (path && (std::string(path, pathLength) == "print" || std::string(path, pathLength) == "warn"))
        return 0;
    const int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    return lua_callyieldable(L, arguments, LUA_MULTRET);
}

void pushCallWrapper(lua_State* L, int originalIndex, const std::string& path, EnvLogger* logger)
{
    lua_pushvalue(L, originalIndex);
    lua_pushlstring(L, path.data(), path.size());
    lua_pushlightuserdata(L, logger);
    lua_pushcclosurek(L, loggedCall, path.c_str(), 3, yieldableContinuation);
}

int blockedSideEffect(lua_State* L)
{
    EnvLogger* logger = static_cast<EnvLogger*>(lua_touserdata(L, lua_upvalueindex(2)));
    size_t pathLength = 0;
    const char* path = lua_tolstring(L, lua_upvalueindex(1), &pathLength);
    if (logger && path)
        logger->record(L, "blocked-call", std::string(path, pathLength), 1, lua_gettop(L), true);
    if (path && (std::string(path) == "getclipboard" || std::string(path) == "clipboard_get"))
    {
        lua_pushliteral(L, "");
        return 1;
    }
    return 0;
}

int fakeNetwork(lua_State* L)
{
    EnvLogger* logger = static_cast<EnvLogger*>(lua_touserdata(L, lua_upvalueindex(2)));
    size_t pathLength = 0;
    const char* path = lua_tolstring(L, lua_upvalueindex(1), &pathLength);
    const std::string api = path ? std::string(path, pathLength) : "http";
    if (logger)
        logger->record(L, "blocked-network", api, 1, lua_gettop(L), true);
    if (api.find("HttpGet") != std::string::npos || api.find("GetAsync") != std::string::npos ||
        api.find("PostAsync") != std::string::npos)
    {
        lua_pushliteral(L, "");
        return 1;
    }
    lua_newtable(L);
    lua_pushboolean(L, 0); lua_setfield(L, -2, "Success");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "StatusCode");
    lua_pushliteral(L, "blocked by --envlogger"); lua_setfield(L, -2, "StatusMessage");
    lua_pushliteral(L, ""); lua_setfield(L, -2, "Body");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "ok");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "status");
    lua_pushliteral(L, ""); lua_setfield(L, -2, "body");
    return 1;
}

void setFakeNetwork(lua_State* L, int table, const char* key, const std::string& api, EnvLogger* logger)
{
    lua_pushlstring(L, api.data(), api.size());
    lua_pushlightuserdata(L, logger);
    lua_pushcclosure(L, fakeNetwork, api.c_str(), 2);
    lua_setfield(L, table, key);
}

void setBlockedSideEffect(lua_State* L, const char* name, EnvLogger* logger)
{
    lua_pushstring(L, name);
    lua_pushlightuserdata(L, logger);
    lua_pushcclosure(L, blockedSideEffect, name, 2);
    lua_setglobal(L, name);
}

int loggedNamecall(lua_State* L)
{
    EnvLogger* logger = context(L);
    const char* method = lua_activenamecallatom(L);
    const std::string name = method ? method : "<unknown>";
    const std::string api = "Instance:" + name;
    const bool network = name == "HttpGet" || name == "HttpGetAsync" ||
        name == "GetAsync" || name == "PostAsync" || name == "RequestAsync";
    if (logger)
        logger->record(L, network ? "blocked-network" : "namecall", api, 1, lua_gettop(L), network);
    if (network)
    {
        if (name == "RequestAsync")
        {
            lua_newtable(L);
            lua_pushboolean(L, 0); lua_setfield(L, -2, "Success");
            lua_pushinteger(L, 0); lua_setfield(L, -2, "StatusCode");
            lua_pushliteral(L, "blocked by --envlogger"); lua_setfield(L, -2, "StatusMessage");
            lua_pushliteral(L, ""); lua_setfield(L, -2, "Body");
        }
        else
            lua_pushliteral(L, "");
        return 1;
    }

    const int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    return lua_callyieldable(L, arguments, LUA_MULTRET);
}

int loggedIndex(lua_State* L)
{
    EnvLogger* logger = context(L);
    const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tolstring(L, 2, nullptr) : nullptr;
    if (logger)
        logger->record(L, "member-read", key ? std::string("Instance.") + key : "Instance.[non-string]", 1, 2, false);
    if (key && (std::string(key) == "HttpGet" || std::string(key) == "HttpGetAsync" ||
                std::string(key) == "GetAsync" || std::string(key) == "PostAsync" ||
                std::string(key) == "RequestAsync"))
    {
        const std::string api = std::string("Instance.") + key;
        lua_pushlstring(L, api.data(), api.size());
        lua_pushlightuserdata(L, logger);
        lua_pushcclosure(L, fakeNetwork, api.c_str(), 2);
        return 1;
    }
    const int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    return lua_callyieldable(L, arguments, LUA_MULTRET);
}

void wrapInstanceMetamethod(lua_State* L, int metatable, const char* name,
                            lua_CFunction wrapper, EnvLogger* logger)
{
    lua_getfield(L, metatable, name);
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    if (lua_getreadonly(L, metatable))
    {
        lua_pop(L, 1);
        return;
    }
    lua_pushvalue(L, -1);
    lua_pushstring(L, name);
    lua_pushlightuserdata(L, logger);
    lua_pushcclosurek(L, wrapper, name, 3, yieldableContinuation);
    lua_setfield(L, metatable, name);
    lua_pop(L, 1);
}

void hookInstanceMetatable(lua_State* L, EnvLogger* logger)
{
    lua_getglobal(L, "game");
    if (lua_getmetatable(L, -1) && lua_istable(L, -1))
    {
        const int metatable = lua_absindex(L, -1);
        wrapInstanceMetamethod(L, metatable, "__namecall", loggedNamecall, logger);
        wrapInstanceMetamethod(L, metatable, "__index", loggedIndex, logger);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

bool shouldWrapGlobal(const std::string& name)
{
    static const std::set<std::string> helpers = {
        "assert", "collectgarbage", "error", "getmetatable", "ipairs", "next",
        "pairs", "pcall", "rawequal", "rawget", "rawlen", "rawset", "select",
        "setmetatable", "tonumber", "tostring", "type", "typeof", "unpack",
        "xpcall"
    };
    return !name.empty() && name != "_G" && helpers.find(name) == helpers.end();
}

void wrapGlobalFunctions(lua_State* L, EnvLogger* logger)
{
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    const int globals = lua_absindex(L, -1);
    std::vector<std::string> names;
    lua_pushnil(L);
    while (lua_next(L, globals) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1))
        {
            size_t length = 0;
            const char* key = lua_tolstring(L, -2, &length);
            if (key)
            {
                std::string name(key, length);
                if (shouldWrapGlobal(name)) names.push_back(std::move(name));
            }
        }
        lua_pop(L, 1);
    }
    std::sort(names.begin(), names.end());
    std::unordered_set<const void*> wrapped;
    for (const std::string& name : names)
    {
        lua_getfield(L, globals, name.c_str());
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            continue;
        }
        const void* identity = lua_topointer(L, -1);
        if (!identity || !wrapped.insert(identity).second)
        {
            lua_pop(L, 1);
            continue;
        }
        if (name == "request" || name == "http_request" || name == "HttpGet" || name == "HttpGetAsync" ||
            name == "setclipboard" || name == "toclipboard" || name == "getclipboard" ||
            name == "clipboard_set" || name == "clipboard_get" || name == "saveinstance" ||
            name == "saveplace" || name == "savegame" || name == "queue_on_teleport" || name == "messagebox")
        {
            lua_pop(L, 1);
            continue;
        }
        pushCallWrapper(L, -1, name, logger);
        lua_pushlstring(L, name.data(), name.size());
        lua_insert(L, -2);
        lua_rawset(L, globals);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void wrapTableFunctions(lua_State* L, int table, const std::string& prefix,
                        EnvLogger* logger, int depth, std::unordered_set<const void*>& seenTables,
                        std::unordered_set<const void*>& seenFunctions)
{
    if (depth > 3 || !lua_istable(L, table) || lua_getreadonly(L, table))
        return;
    table = lua_absindex(L, table);
    const void* identity = lua_topointer(L, table);
    if (!identity || !seenTables.insert(identity).second)
        return;

    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            size_t keyLength = 0;
            const char* keyData = lua_tolstring(L, -2, &keyLength);
            const std::string key = keyData ? std::string(keyData, keyLength) : std::string();
            const std::string path = prefix.empty() ? key : prefix + "." + key;
            if (lua_isfunction(L, -1))
            {
                const void* function = lua_topointer(L, -1);
                const bool networkStub = path == "http.request" || path == "http.get" || path == "http.post" || path == "syn.request";
                if (!networkStub && function && seenFunctions.insert(function).second)
                {
                    pushCallWrapper(L, -1, path, logger);
                    lua_pushlstring(L, key.data(), key.size());
                    lua_insert(L, -2);
                    lua_rawset(L, table);
                }
            }
            else if (lua_istable(L, -1))
            {
                lua_pushvalue(L, -1);
                wrapTableFunctions(L, -1, path, logger, depth + 1, seenTables, seenFunctions);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    seenTables.erase(identity);
}

void wrapApiTables(lua_State* L, EnvLogger* logger)
{
    const char* roots[] = {"http", "syn", "crypt", "base64", "Drawing", "task", nullptr};
    std::unordered_set<const void*> seenTables;
    std::unordered_set<const void*> seenFunctions;
    for (const char** root = roots; *root; ++root)
    {
        lua_getglobal(L, *root);
        if (lua_istable(L, -1))
            wrapTableFunctions(L, -1, *root, logger, 0, seenTables, seenFunctions);
        lua_pop(L, 1);
    }
}

void restoreSafeGlobals(lua_State* L, int saved, const char* const* names)
{
    for (const char* const* name = names; *name; ++name)
    {
        lua_getfield(L, saved, *name);
        if (!lua_isnil(L, -1))
            lua_setglobal(L, *name);
        else
            lua_pop(L, 1);
    }
}

} // namespace

void installEnvLogger(lua_State* L, const std::string& logPath)
{
    std::unique_ptr<EnvLogger> logger = std::make_unique<EnvLogger>(logPath);
    EnvLogger* ctx = logger.get();
    lua_pushlightuserdata(L, logger.release());
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoggerRegistryKey);

    // Save the dynamic compiler, getfenv helpers, and the prelude's process-local
    // in-memory files before applying the ordinary restricted sandbox.
    lua_newtable(L);
    const int saved = lua_absindex(L, -1);
    const char* const safe[] = {
        "loadstring", "load", "loadfile", "getfenv", "setfenv", "getgenv", "getrenv", "getsenv",
        "getnamecallmethod", "setnamecallmethod", "checkcaller", "cloneref", "clonereference",
        "readfile", "writefile", "isfile", "isfolder", "makefolder", "delfile", "delfolder",
        "listfiles", "appendfile", "getconnections", "gethui", "protectgui", "identifyexecutor",
        "getexecutorname", "setfflag", "getfflag", "firetouchdetector", "firetouchinterest", nullptr
    };
    for (const char* const* name = safe; *name; ++name)
    {
        lua_getglobal(L, *name);
        lua_setfield(L, saved, *name);
    }
    applySandbox(L);
    restoreSafeGlobals(L, saved, safe);
    lua_pop(L, 1);

    // Keep Luau's native debug functions and Lumora's registered reflection
    // helpers available for compatibility checks. Host-backed side effects
    // remain blocked during an envlogger run; the file API above is in memory.
    const char* denied[] = {
        "setclipboard", "toclipboard", "getclipboard", "clipboard_set", "clipboard_get",
        "saveinstance", "saveplace", "savegame", "queue_on_teleport", "messagebox", nullptr
    };
    for (const char** name = denied; *name; ++name)
        setBlockedSideEffect(L, *name, ctx);

    const char* networks[] = {"request", "http_request", "HttpGet", "HttpGetAsync", nullptr};
    for (const char** name = networks; *name; ++name)
    {
        lua_pushstring(L, *name);
        lua_pushlightuserdata(L, ctx);
        lua_pushcclosure(L, fakeNetwork, *name, 2);
        lua_setglobal(L, *name);
    }
    lua_newtable(L);
    const int http = lua_absindex(L, -1);
    setFakeNetwork(L, http, "request", "http.request", ctx);
    setFakeNetwork(L, http, "get", "http.get", ctx);
    setFakeNetwork(L, http, "post", "http.post", ctx);
    lua_setglobal(L, "http");
    lua_newtable(L);
    const int syn = lua_absindex(L, -1);
    setFakeNetwork(L, syn, "request", "syn.request", ctx);
    lua_setglobal(L, "syn");
    hookInstanceMetatable(L, ctx);
    wrapGlobalFunctions(L, ctx);
    wrapApiTables(L, ctx);
}

void finishEnvLogger(lua_State* L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kLoggerRegistryKey);
    auto* logger = static_cast<EnvLogger*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!logger)
        return;

    if (logger->truncated && logger->bytes + 128 < kMaxLogBytes)
    {
        const std::string summary = "{\"event\":\"summary\",\"events\":" +
            std::to_string(logger->events) + ",\"blockedNetwork\":" +
            std::to_string(logger->blockedNetwork) + ",\"truncated\":true}\n";
        logger->output.write(summary.data(), std::streamsize(summary.size()));
    }
    else if (logger->bytes + 128 < kMaxLogBytes)
    {
        const std::string summary = "{\"event\":\"summary\",\"events\":" +
            std::to_string(logger->events) + ",\"blockedNetwork\":" +
            std::to_string(logger->blockedNetwork) + ",\"truncated\":false}\n";
        logger->output.write(summary.data(), std::streamsize(summary.size()));
    }
    logger->output.flush();
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoggerRegistryKey);
    delete logger;
}
