// On Windows curl/curl.h transitively includes windows.h, which defines the
// min/max macros that break std::numeric_limits<T>::max() and any std::min/max
// use below. NOMINMAX must be defined before that header is first pulled in.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <openssl/evp.h>
#include <curl/curl.h>
#include <lz4.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#undef luaL_error
#undef luaL_argerror
#define luaL_error(L, ...) (luaL_errorL(L, __VA_ARGS__), 0)
#define luaL_argerror(L, narg, message) (luaL_argerrorL(L, narg, message), 0)

namespace
{
constexpr const char* kRuntimeName = "Lumora";
constexpr const char* kRuntimeVersion = "0.7.0";
constexpr int kDefaultIdentity = 2;
constexpr int kMinIdentity = 0;
constexpr int kMaxIdentity = 8;
constexpr size_t kMaxDecompressedBytes = 64u * 1024u * 1024u;

std::atomic<int> g_fpsCap{0};
std::mutex g_identityMutex;
std::unordered_map<const void*, int> g_threadIdentities;
std::once_flag g_curlInit;

void registerFunction(lua_State* L, int table, const char* name, lua_CFunction function)
{
    table = lua_absindex(L, table);
    lua_pushcfunction(L, function, name);
    lua_setfield(L, table, name);
}

bool isInstance(lua_State* L, int index)
{
    if (!lua_istable(L, index))
        return false;

    index = lua_absindex(L, index);
    lua_pushliteral(L, "__type");
    lua_rawget(L, index);
    const bool result = lua_isstring(L, -1) && std::strcmp(lua_tostring(L, -1), "Instance") == 0;
    lua_pop(L, 1);
    return result;
}

std::string requiredString(lua_State* L, int index)
{
    size_t length = 0;
    const char* value = luaL_checklstring(L, index, &length);
    return std::string(value, length);
}

std::string trim(std::string value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

bool rawStringField(lua_State* L, int table, const char* field, std::string& value)
{
    table = lua_absindex(L, table);
    lua_rawgetfield(L, table, field);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    size_t length = 0;
    const char* string = luaL_checklstring(L, -1, &length);
    value.assign(string, length);
    lua_pop(L, 1);
    return true;
}

bool rawBooleanField(lua_State* L, int table, const char* field, bool& value)
{
    table = lua_absindex(L, table);
    lua_rawgetfield(L, table, field);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return true;
}

bool rawNumberField(lua_State* L, int table, const char* field, double& value)
{
    table = lua_absindex(L, table);
    lua_rawgetfield(L, table, field);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    value = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return true;
}

void ensureLumoraTable(lua_State* L)
{
    lua_getglobal(L, "lumora");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "lumora");
    }
}

void ensureTableField(lua_State* L, int table, const char* field)
{
    table = lua_absindex(L, table);
    lua_getfield(L, table, field);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, table, field);
    }
}

void setInternalSource(lua_State* L, int instance, const std::string& source)
{
    instance = lua_absindex(L, instance);
    lua_pushlstring(L, source.data(), source.size());
    lua_rawsetfield(L, instance, "_lumora_source");
}

bool getInternalSource(lua_State* L, int index, std::string& source)
{
    if (!isInstance(L, index))
        luaL_argerror(L, index, "Script expected");

    index = lua_absindex(L, index);
    lua_rawgetfield(L, index, "_lumora_source");
    if (!lua_isstring(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }

    size_t length = 0;
    const char* content = lua_tolstring(L, -1, &length);
    source.assign(content, length);
    lua_pop(L, 1);
    return true;
}

void setInstanceProperty(lua_State* L, int instance, const char* field, const std::string& value)
{
    instance = lua_absindex(L, instance);
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, instance, field);
}

bool makeInstance(lua_State* L, const char* className)
{
    const int base = lua_gettop(L);
    lua_getglobal(L, "Instance");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, base);
        return false;
    }
    lua_getfield(L, -1, "new");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, base);
        return false;
    }
    lua_pushstring(L, className);
    if (lua_pcall(L, 1, 1, 0) != 0)
    {
        lua_settop(L, base);
        return false;
    }
    lua_remove(L, base + 1); // remove Instance table; leave the instance at base + 1
    return true;
}

void setInstanceParent(lua_State* L, int instance, const char* serviceName)
{
    const int top = lua_gettop(L);
    instance = lua_absindex(L, instance);

    lua_getglobal(L, "game");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return;
    }
    lua_getfield(L, -1, "GetService");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, top);
        return;
    }
    lua_pushvalue(L, -2);
    lua_pushstring(L, serviceName);
    if (lua_pcall(L, 2, 1, 0) != 0 || !isInstance(L, -1))
    {
        lua_settop(L, top);
        return;
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, instance, "Parent");
    lua_settop(L, top);
}

void appendLumoraArray(lua_State* L, const char* field, int valueIndex)
{
    valueIndex = lua_absindex(L, valueIndex);
    ensureLumoraTable(L);
    const int lumora = lua_gettop(L);
    ensureTableField(L, lumora, field);
    const int array = lua_gettop(L);
    const int count = int(lua_objlen(L, array));
    lua_pushvalue(L, valueIndex);
    lua_rawseti(L, array, count + 1);
    lua_pop(L, 2);
}

void recordFunctionSource(lua_State* L, int functionIndex, const std::string& source)
{
    functionIndex = lua_absindex(L, functionIndex);
    ensureLumoraTable(L);
    const int lumora = lua_gettop(L);
    ensureTableField(L, lumora, "_functionSources");
    const int sources = lua_gettop(L);
    lua_pushvalue(L, functionIndex);
    lua_pushlstring(L, source.data(), source.size());
    lua_rawset(L, sources);
    lua_pop(L, 2);
}

bool getFunctionSource(lua_State* L, int functionIndex, std::string& source)
{
    functionIndex = lua_absindex(L, functionIndex);
    lua_getglobal(L, "lumora");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, "_functionSources");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 2);
        return false;
    }
    lua_pushvalue(L, functionIndex);
    lua_rawget(L, -2);
    if (!lua_isstring(L, -1))
    {
        lua_pop(L, 3);
        return false;
    }
    size_t length = 0;
    const char* data = lua_tolstring(L, -1, &length);
    source.assign(data, length);
    lua_pop(L, 3);
    return true;
}

std::string sha256(const std::string& input)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &length, EVP_sha256(), nullptr) != 1)
        return {};

    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(length * 2);
    for (unsigned int i = 0; i < length; ++i)
    {
        output.push_back(kHex[digest[i] >> 4]);
        output.push_back(kHex[digest[i] & 0x0f]);
    }
    return output;
}

int unsupported(lua_State* L, const char* feature, const char* reason)
{
    return luaL_error(L, "%s is unavailable in Lumora: %s", feature, reason);
}

int identifyExecutor(lua_State* L)
{
    lua_pushstring(L, kRuntimeName);
    lua_pushstring(L, kRuntimeVersion);
    return 2;
}

int getExecutorName(lua_State* L)
{
    lua_pushstring(L, kRuntimeName);
    return 1;
}

int getGlobalEnvironment(lua_State* L)
{
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    return 1;
}

int getScriptEnvironment(lua_State* L)
{
    std::string source;
    if (!getInternalSource(L, 1, source))
        return luaL_error(L, "getsenv cannot inspect this Script: Lumora has no execution environment for it");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    return 1;
}

int getRawMetatable(lua_State* L)
{
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1))
        lua_pushnil(L);
    return 1;
}

int setRawMetatable(lua_State* L)
{
    luaL_checkany(L, 1);
    if (!lua_isnil(L, 2))
        luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushvalue(L, 2);
    if (!lua_setmetatable(L, 1))
        return luaL_error(L, "setrawmetatable cannot set a metatable on this value");
    lua_pushvalue(L, 1);
    return 1;
}

int setReadonly(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    lua_setreadonly(L, 1, lua_toboolean(L, 2));
    return 0;
}

int isReadonly(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, lua_getreadonly(L, 1));
    return 1;
}

int makeWriteable(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_setreadonly(L, 1, false);
    return 0;
}

// hookfunction(original, hook) -> old : installs a real VM-level detour. Every
// call that reaches `original` through any reference (globals, upvalues,
// fields, bytecode CALL and NAMECALL dispatch) is redirected to `hook`. The
// returned `old` is a fresh clone that runs the original body without the
// detour, which is what the executor contract (Calamari: "returns a copy of
// the original function") requires.
int hookfunctionImpl(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    // hook must be a function or nil/none (nil removes an existing detour,
    // restoring calls to the original body)
    if (lua_gettop(L) < 2 || (!lua_isfunction(L, 2) && !lua_isnoneornil(L, 2)))
        return luaL_argerror(L, 2, "function or nil expected");

    lua_sethookclosure(L, 1, lua_isnoneornil(L, 2) ? 0 : 2);
    // hand back a detour-free clone of the original body: calling it from
    // inside the hook must not re-enter the hook
    lua_clonefunctionany(L, 1);
    return 1;
}

// hookmetamethod(obj, method, hook) -> old : detours a metamethod of obj (or
int hookmetamethodImpl(lua_State* L)
{
    luaL_checkany(L, 1);
    size_t length = 0;
    const char* method = luaL_checklstring(L, 2, &length);
    // hook must be a function, or nil/none to restore the original
    if (lua_gettop(L) < 3 || (!lua_isfunction(L, 3) && !lua_isnoneornil(L, 3)))
        return luaL_argerror(L, 3, "function or nil expected");

    // validate the metamethod name against the VM's tag-method list so
    // nonexistent methods fail like on a real client, not silently
    static const char* const kValid[] = {
        "__index", "__newindex", "__call", "__concat", "__unm", "__add", "__sub",
        "__mul", "__div", "__mod", "__pow", "__tostring", "__eq", "__lt", "__le",
        "__iter", "__len", "__namecall", "__type", "__close",
    };
    bool valid = false;
    for (const char* candidate : kValid)
        if (std::strcmp(candidate, method) == 0)
        {
            valid = true;
            break;
        }
    if (!valid)
        return luaL_error(L, "hookmetamethod: '%s' is not a valid metamethod", method);

    // resolve the metatable: obj's own, or the base-type metatable for
    // primitives (matching getrawmetatable's view of the runtime)
    if (!lua_getmetatable(L, 1))
        return luaL_error(L, "hookmetamethod: cannot hook metamethod '%s' of a nil value", method);
    const int metatable = lua_gettop(L);

    lua_getfield(L, metatable, method);
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2); // method value + metatable
        return luaL_error(L, "hookmetamethod: metamethod '%s' is not defined", method);
    }

    // hook must be a function or nil/none (nil restores the original)
    if (lua_gettop(L) < 3 || (!lua_isfunction(L, 3) && !lua_isnoneornil(L, 3)))
    {
        lua_pop(L, 2); // method value + metatable
        return luaL_argerror(L, 3, "function or nil expected");
    }

    // stack: obj(1), method(2), hook(3), metatable(4), original metamethod(5)
    lua_sethookclosure(L, lua_gettop(L), lua_isnoneornil(L, 3) ? 0 : 3); // detour original -> hook
    lua_clonefunctionany(L, lua_gettop(L)); // clone of the original metamethod (detour-free)

    // leave only the clone as the result
    lua_replace(L, 3);
    lua_settop(L, 3);
    return 1;
}

// getnamecallmethod() -> string : the method name of the active NAMECALL
// dispatch. Errors outside a namecall frame, matching real executors that
// only expose it inside __namecall hooks (Sentinel docs).
int getNamecallMethodImpl(lua_State* L)
{
    const char* name = lua_activenamecallatom(L);
    if (!name)
        return luaL_error(L, "getnamecallmethod is only available inside a namecall hook");
    lua_pushstring(L, name);
    return 1;
}

// setnamecallmethod(name) -> bool : rewrites the method the active NAMECALL
// dispatch resolves; the engine dispatcher re-reads it, so calls can be
// rerouted (Sentinel's FireServer -> InvokeServer example).
int setNamecallMethodImpl(lua_State* L)
{
    size_t length = 0;
    const char* name = luaL_checklstring(L, 1, &length);
    if (length == 0)
        return luaL_argerror(L, 1, "method name must not be empty");
    lua_pushboolean(L, lua_setnamecallatom(L, name) != 0);
    return 1;
}

// getconstants(f | level) -> { ... } : constant table of a Lua function (or
// the function at stack level, aliasing debug.getconstants like Synapse X).
// C closures have no constants: executors report an empty table.
int getconstantsImpl(lua_State* L)
{
    luaL_checkany(L, 1);
    if (lua_isnumber(L, 1))
    {
        const lua_Integer level = lua_tointeger(L, 1);
        if (level < 0)
            return luaL_argerror(L, 1, "level must be a non-negative integer");
        lua_Debug ar{};
        if (lua_getinfo(L, int(level), "f", &ar) == 0 || !lua_isfunction(L, -1))
            return luaL_error(L, "getconstants: no function at level %d", int(level));
    }
    else if (!lua_isfunction(L, 1))
        return luaL_argerror(L, 1, "function or level expected, got no value");

    const int funcIndex = lua_gettop(L); // target function: arg 1 or pushed by getinfo
    const int count = lua_getfunctionconstants(L, funcIndex);
    if (count < 0)
    {
        lua_settop(L, funcIndex - 1); // drop the pushed function
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, count, 0);
    const int output = lua_gettop(L);
    // constants sit at funcIndex+1 .. funcIndex+count; fill in reverse so
    // rawseti pops the right value each time
    for (int i = count; i >= 1; --i)
    {
        lua_pushvalue(L, funcIndex + i);
        lua_rawseti(L, output, i);
    }
    lua_settop(L, output);
    return 1;
}

// getprotos(f | level) -> { functions } : fresh executable closures for the
// nested protos (local functions) of a Lua function, in definition order
// (Sentinel docs). C closures have no nested protos: empty table.
int getprotosImpl(lua_State* L)
{
    luaL_checkany(L, 1);
    if (lua_isnumber(L, 1))
    {
        const lua_Integer level = lua_tointeger(L, 1);
        if (level < 0)
            return luaL_argerror(L, 1, "level must be a non-negative integer");
        lua_Debug ar{};
        if (lua_getinfo(L, int(level), "f", &ar) == 0 || !lua_isfunction(L, -1))
            return luaL_error(L, "getprotos: no function at level %d", int(level));
    }
    else if (!lua_isfunction(L, 1))
        return luaL_argerror(L, 1, "function or level expected, got no value");

    const int funcIndex = lua_gettop(L);
    const int count = lua_getfunctionprotos(L, funcIndex);
    if (count < 0)
    {
        lua_settop(L, funcIndex - 1);
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, count, 0);
    const int output = lua_gettop(L);
    for (int i = count; i >= 1; --i)
    {
        lua_pushvalue(L, funcIndex + i);
        lua_rawseti(L, output, i);
    }
    lua_settop(L, output);
    return 1;
}

int compileScriptBytecode(lua_State* L)
{
    std::string source;
    if (!getInternalSource(L, 1, source))
        return luaL_error(L, "getscriptbytecode cannot read source from this Script");

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 2;
    const std::string bytecode = Luau::compile(source, options);
    lua_pushlstring(L, bytecode.data(), bytecode.size());
    return 1;
}

int decompileScript(lua_State* L)
{
    std::string source;
    if (!getInternalSource(L, 1, source))
        return luaL_error(L, "decompile cannot recover source from this Script");
    lua_pushlstring(L, source.data(), source.size());
    return 1;
}

int getScriptClosure(lua_State* L)
{
    std::string source;
    if (!getInternalSource(L, 1, source))
        return luaL_error(L, "getscriptclosure cannot read source from this Script");

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 2;
    const std::string bytecode = Luau::compile(source, options);
    if (luau_load(L, "=Lumora.Script", bytecode.data(), bytecode.size(), 0) != 0)
    {
        const char* error = lua_tostring(L, -1);
        return luaL_error(L, "getscriptclosure could not load Script bytecode: %s", error ? error : "unknown error");
    }
    recordFunctionSource(L, -1, source);
    return 1;
}

int getUpvalues(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_newtable(L);
    const int output = lua_gettop(L);
    int outputIndex = 0;
    for (int index = 1;; ++index)
    {
        if (!lua_getupvalue(L, 1, index))
            break;
        lua_rawseti(L, output, ++outputIndex);
    }
    return 1;
}

int unsupportedVmReflection(lua_State* L)
{
    return unsupported(L, lua_tostring(L, lua_upvalueindex(1)), "the public Luau VM API does not expose compiled constants or nested protos");
}

int getCallbackValue(lua_State* L)
{
    if (!isInstance(L, 1))
        return luaL_argerror(L, 1, "Instance expected");
    const std::string property = requiredString(L, 2);
    lua_getfield(L, 1, property.c_str());
    if (!lua_isfunction(L, -1))
        return luaL_error(L, "getcallbackvalue expected function-valued property '%s'", property.c_str());
    return 1;
}

int fireSignal(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "Fire");
    if (!lua_isfunction(L, -1))
        return luaL_argerror(L, 1, "Lumora Signal expected");

    const int argc = lua_gettop(L) - 2;
    lua_pushvalue(L, 1);
    for (int i = 2; i <= argc + 1; ++i)
        lua_pushvalue(L, i);
    lua_call(L, argc + 1, 0);
    return 0;
}

int getConnections(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgetfield(L, 1, "_connections");
    if (!lua_istable(L, -1))
        return luaL_argerror(L, 1, "Lumora Signal expected");

    const int connections = lua_gettop(L);
    lua_newtable(L);
    const int output = lua_gettop(L);
    int result = 0;
    const int length = int(lua_objlen(L, connections));
    for (int i = 1; i <= length; ++i)
    {
        lua_rawgeti(L, connections, i); // record
        if (lua_istable(L, -1))
        {
            const int record = lua_gettop(L);
            lua_rawgetfield(L, record, "c");
            if (lua_istable(L, -1))
            {
                const int connection = lua_gettop(L);
                lua_rawgetfield(L, record, "fn");
                if (lua_isfunction(L, -1))
                    lua_setfield(L, connection, "Function");
                else
                    lua_pop(L, 1);
                lua_pushvalue(L, connection);
                lua_rawseti(L, output, ++result);
            }
            lua_pop(L, 1); // connection or nil
        }
        lua_pop(L, 1); // record
    }
    return 1;
}

int fireClickDetector(lua_State* L)
{
    if (!isInstance(L, 1))
        return luaL_argerror(L, 1, "ClickDetector expected");

    std::string className;
    lua_rawgetfield(L, 1, "_properties");
    if (lua_istable(L, -1))
        rawStringField(L, -1, "ClassName", className);
    lua_pop(L, 1);
    if (className != "ClickDetector")
        return luaL_argerror(L, 1, "ClickDetector expected");

    const double distance = luaL_optnumber(L, 2, 0.0);
    lua_getfield(L, 1, "MaxActivationDistance");
    const double maximum = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 32.0;
    lua_pop(L, 1);
    if (distance > maximum)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* eventName = luaL_optstring(L, 3, "MouseClick");
    lua_getfield(L, 1, eventName);
    if (!lua_istable(L, -1))
        return luaL_error(L, "ClickDetector event '%s' is unavailable", eventName);
    const int signal = lua_gettop(L);

    lua_getglobal(L, "game");
    lua_getfield(L, -1, "GetService");
    lua_pushvalue(L, -2);
    lua_pushliteral(L, "Players");
    lua_call(L, 2, 1);
    lua_getfield(L, -1, "LocalPlayer");
    const int player = lua_gettop(L);

    lua_getfield(L, signal, "Fire");
    lua_pushvalue(L, signal);
    lua_pushvalue(L, player);
    lua_call(L, 2, 0);
    lua_pushboolean(L, 1);
    return 1;
}

int getInstances(lua_State* L)
{
    lua_getglobal(L, "lumora");
    lua_getfield(L, -1, "_allInstances");
    if (!lua_istable(L, -1))
        return luaL_error(L, "Lumora instance registry is unavailable");

    const int registry = lua_gettop(L);
    lua_newtable(L);
    const int output = lua_gettop(L);
    int result = 0;
    lua_pushnil(L);
    while (lua_next(L, registry) != 0)
    {
        if (isInstance(L, -2))
        {
            lua_pushvalue(L, -2);
            lua_rawseti(L, output, ++result);
        }
        lua_pop(L, 1);
    }
    return 1;
}

int getNilInstances(lua_State* L)
{
    lua_getglobal(L, "lumora");
    lua_getfield(L, -1, "_allInstances");
    if (!lua_istable(L, -1))
        return luaL_error(L, "Lumora instance registry is unavailable");
    const int registry = lua_gettop(L);

    lua_getglobal(L, "game");
    const int game = lua_gettop(L);
    lua_newtable(L);
    const int output = lua_gettop(L);
    int result = 0;

    lua_pushnil(L);
    while (lua_next(L, registry) != 0)
    {
        if (isInstance(L, -2) && !lua_rawequal(L, -2, game))
        {
            lua_rawgetfield(L, -2, "_parent");
            const bool parentless = lua_isnil(L, -1);
            lua_pop(L, 1);
            if (parentless)
            {
                lua_pushvalue(L, -2);
                lua_rawseti(L, output, ++result);
            }
        }
        lua_pop(L, 1);
    }
    return 1;
}

int getLoadedModules(lua_State* L)
{
    lua_getglobal(L, "lumora");
    lua_getfield(L, -1, "_loadedModules");
    if (!lua_istable(L, -1))
    {
        lua_newtable(L);
        return 1;
    }

    const int modules = lua_gettop(L);
    lua_newtable(L);
    const int output = lua_gettop(L);
    const int count = int(lua_objlen(L, modules));
    for (int index = 1; index <= count; ++index)
    {
        lua_rawgeti(L, modules, index);
        lua_rawseti(L, output, index);
    }
    return 1;
}

int getCustomAssetUnavailable(lua_State* L)
{
    return unsupported(L, "getcustomasset", "internal registration error");
}

struct HttpResponse
{
    std::string body;
    std::map<std::string, std::string> headers;
    std::string reason;
    long status = 0;
};

size_t appendHttpBody(char* data, size_t size, size_t count, void* context)
{
    const size_t length = size * count;
    static_cast<HttpResponse*>(context)->body.append(data, length);
    return length;
}

size_t appendHttpHeader(char* data, size_t size, size_t count, void* context)
{
    const size_t length = size * count;
    std::string line(data, length);
    HttpResponse& response = *static_cast<HttpResponse*>(context);
    if (line.rfind("HTTP/", 0) == 0)
    {
        const size_t firstSpace = line.find(' ');
        const size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : line.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos)
            response.reason = trim(line.substr(secondSpace + 1));
        return length;
    }
    const size_t separator = line.find(':');
    if (separator != std::string::npos)
    {
        const std::string name = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (!name.empty())
            response.headers[name] = value;
    }
    return length;
}

bool getRequestString(lua_State* L, int options, const char* primary, const char* alternate, std::string& output)
{
    options = lua_absindex(L, options);
    lua_getfield(L, options, primary);
    if (lua_isnil(L, -1) && alternate)
    {
        lua_pop(L, 1);
        lua_getfield(L, options, alternate);
    }
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    size_t length = 0;
    const char* value = luaL_checklstring(L, -1, &length);
    output.assign(value, length);
    lua_pop(L, 1);
    return true;
}

void appendRequestHeaders(lua_State* L, int options, const char* primary, const char* alternate, curl_slist*& headers)
{
    options = lua_absindex(L, options);
    lua_getfield(L, options, primary);
    if (!lua_istable(L, -1) && alternate)
    {
        lua_pop(L, 1);
        lua_getfield(L, options, alternate);
    }
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    const int table = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        if (lua_isstring(L, -2) && lua_isstring(L, -1))
        {
            size_t keySize = 0;
            size_t valueSize = 0;
            const char* key = lua_tolstring(L, -2, &keySize);
            const char* value = lua_tolstring(L, -1, &valueSize);
            const std::string line = std::string(key, keySize) + ": " + std::string(value, valueSize);
            headers = curl_slist_append(headers, line.c_str());
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

int httpRequest(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const int options = lua_absindex(L, 1);
    std::string url;
    if (!getRequestString(L, options, "Url", "url", url) || url.empty())
        return luaL_argerror(L, 1, "request options require Url");

    std::string method = "GET";
    getRequestString(L, options, "Method", "method", method);
    std::string body;
    const bool hasBody = getRequestString(L, options, "Body", "body", body);
    bool followRedirects = true;
    rawBooleanField(L, options, "FollowRedirects", followRedirects);
    double timeoutSeconds = 30.0;
    rawNumberField(L, options, "Timeout", timeoutSeconds);
    if (!(timeoutSeconds > 0.0 && timeoutSeconds <= 120.0))
        return luaL_argerror(L, 1, "Timeout must be between 0 and 120 seconds");

    std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl)
        return luaL_error(L, "request could not initialize libcurl");

    HttpResponse response;
    curl_slist* headers = nullptr;
    appendRequestHeaders(L, options, "Headers", "headers", headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, long(timeoutSeconds * 1000.0));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendHttpBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendHttpHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (hasBody)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK)
        return luaL_error(L, "request failed: %s", curl_easy_strerror(code));

    const bool success = response.status >= 200 && response.status < 400;
    lua_newtable(L);
    const int result = lua_gettop(L);
    lua_pushboolean(L, success);
    lua_setfield(L, result, "Success");
    lua_pushinteger(L, response.status);
    lua_setfield(L, result, "StatusCode");
    lua_pushlstring(L, response.reason.data(), response.reason.size());
    lua_setfield(L, result, "StatusMessage");
    lua_pushlstring(L, response.body.data(), response.body.size());
    lua_setfield(L, result, "Body");
    lua_newtable(L);
    const int outputHeaders = lua_gettop(L);
    for (const auto& [name, value] : response.headers)
    {
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, outputHeaders, name.c_str());
    }
    lua_setfield(L, result, "Headers");

    // Lowercase aliases make the result directly usable by the existing native
    // @lumora/net module conventions without changing the Roblox-style fields.
    lua_pushboolean(L, success);
    lua_setfield(L, result, "ok");
    lua_pushinteger(L, response.status);
    lua_setfield(L, result, "status");
    lua_pushlstring(L, response.body.data(), response.body.size());
    lua_setfield(L, result, "body");
    return 1;
}

// http.get / http.post are dedicated convenience aliases over the same
// libcurl-backed request path. They accept either a bare URL string or a full
// options table (Url/Headers/Body/Timeout/FollowRedirects) and force the HTTP
// verb, so callers can use the terse executor idiom without losing any of the
// real request semantics.
int httpVerb(lua_State* L, const char* verb)
{
    if (lua_istable(L, 1))
    {
        lua_pushvalue(L, 1);
    }
    else
    {
        const char* url = luaL_checkstring(L, 1);
        lua_newtable(L);
        lua_pushstring(L, url);
        lua_setfield(L, -2, "Url");
    }
    const int options = lua_gettop(L);
    lua_pushstring(L, verb);
    lua_setfield(L, options, "Method");
    lua_pushvalue(L, options);
    lua_remove(L, options);
    return httpRequest(L);
}

int httpGet(lua_State* L)
{
    return httpVerb(L, "GET");
}

int httpPost(lua_State* L)
{
    return httpVerb(L, "POST");
}

int lz4Compress(lua_State* L)
{
    const std::string input = requiredString(L, 1);
    if (input.size() > size_t(std::numeric_limits<int>::max()))
        return luaL_error(L, "lz4compress input exceeds LZ4 size limit");
    const int bound = LZ4_compressBound(int(input.size()));
    if (bound <= 0)
        return luaL_error(L, "lz4compress could not allocate output bound");
    std::string output(size_t(bound), '\0');
    const int written = LZ4_compress_default(input.data(), output.data(), int(input.size()), bound);
    if (written <= 0)
        return luaL_error(L, "lz4compress failed");
    output.resize(size_t(written));
    lua_pushlstring(L, output.data(), output.size());
    return 1;
}

int lz4Decompress(lua_State* L)
{
    const std::string input = requiredString(L, 1);
    const lua_Integer requestedSize = luaL_checkinteger(L, 2);
    if (requestedSize < 0 || size_t(requestedSize) > kMaxDecompressedBytes)
        return luaL_argerror(L, 2, "uncompressed size must be between 0 and 67108864");
    if (input.size() > size_t(std::numeric_limits<int>::max()))
        return luaL_error(L, "lz4decompress input exceeds LZ4 size limit");

    std::string output(size_t(requestedSize), '\0');
    const int written = LZ4_decompress_safe(input.data(), output.data(), int(input.size()), int(requestedSize));
    if (written < 0 || written != requestedSize)
        return luaL_error(L, "lz4decompress failed: invalid block or uncompressed size");
    lua_pushlstring(L, output.data(), output.size());
    return 1;
}

const void* currentThreadKey(lua_State* L)
{
    lua_pushthread(L);
    const void* key = lua_topointer(L, -1);
    lua_pop(L, 1);
    return key;
}

int getThreadIdentity(lua_State* L)
{
    const void* key = currentThreadKey(L);
    std::lock_guard<std::mutex> lock(g_identityMutex);
    const auto it = g_threadIdentities.find(key);
    lua_pushinteger(L, it == g_threadIdentities.end() ? kDefaultIdentity : it->second);
    return 1;
}

int setThreadIdentity(lua_State* L)
{
    const lua_Integer identity = luaL_checkinteger(L, 1);
    if (identity < kMinIdentity || identity > kMaxIdentity)
        return luaL_argerror(L, 1, "identity must be an integer from 0 through 8");
    const void* key = currentThreadKey(L);
    std::lock_guard<std::mutex> lock(g_identityMutex);
    g_threadIdentities[key] = int(identity);
    return 0;
}

int setFpsCap(lua_State* L)
{
    const lua_Integer cap = luaL_checkinteger(L, 1);
    if (cap < 0 || cap > 1000)
        return luaL_argerror(L, 1, "FPS cap must be an integer from 0 through 1000");
    g_fpsCap.store(int(cap));
    return 0;
}

int getFpsCap(lua_State* L)
{
    lua_pushinteger(L, g_fpsCap.load());
    return 1;
}

int cloneReference(lua_State* L)
{
    luaL_checkany(L, 1);
    lua_pushvalue(L, 1);
    return 1;
}

int compareInstances(lua_State* L)
{
    if (!isInstance(L, 1) || !isInstance(L, 2))
        return luaL_error(L, "compareinstances expects two Instances");
    lua_pushboolean(L, lua_rawequal(L, 1, 2));
    return 1;
}

int getHiddenProperty(lua_State* L)
{
    const std::string property = requiredString(L, 2);
    if (property != "Source")
    {
        lua_pushnil(L);
        lua_pushboolean(L, 0);
        return 2;
    }
    std::string source;
    if (!getInternalSource(L, 1, source))
    {
        lua_pushnil(L);
        lua_pushboolean(L, 0);
        return 2;
    }
    lua_pushlstring(L, source.data(), source.size());
    lua_pushboolean(L, 1);
    return 2;
}

int setHiddenProperty(lua_State* L)
{
    const std::string property = requiredString(L, 2);
    if (property != "Source")
        return luaL_error(L, "sethiddenproperty supports only Lumora's internal Script Source property");
    const std::string source = requiredString(L, 3);
    if (!isInstance(L, 1))
        return luaL_argerror(L, 1, "Script expected");
    setInternalSource(L, 1, source);
    lua_pushboolean(L, 1);
    return 1;
}

int getRunningScripts(lua_State* L)
{
    lua_getglobal(L, "lumora");
    lua_getfield(L, -1, "_runningScripts");
    if (!lua_istable(L, -1))
    {
        lua_newtable(L);
        return 1;
    }
    const int scripts = lua_gettop(L);
    lua_newtable(L);
    const int output = lua_gettop(L);
    const int count = int(lua_objlen(L, scripts));
    for (int index = 1; index <= count; ++index)
    {
        lua_rawgeti(L, scripts, index);
        lua_rawseti(L, output, index);
    }
    return 1;
}

int getScriptHash(lua_State* L)
{
    std::string source;
    if (!getInternalSource(L, 1, source))
        return luaL_error(L, "getscripthash cannot read source from this Script");
    const std::string hash = sha256(source);
    if (hash.empty())
        return luaL_error(L, "getscripthash could not compute SHA-256");
    lua_pushlstring(L, hash.data(), hash.size());
    return 1;
}

int getFunctionHash(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    std::string source;
    if (!getFunctionSource(L, 1, source))
        return luaL_error(L, "getfunctionhash requires a closure returned by getscriptclosure; Luau does not expose arbitrary closure bytecode");
    const std::string hash = sha256(source);
    if (hash.empty())
        return luaL_error(L, "getfunctionhash could not compute SHA-256");
    lua_pushlstring(L, hash.data(), hash.size());
    return 1;
}

int isExecutorClosure(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushboolean(L, lua_iscfunction(L, 1));
    return 1;
}
} // namespace

void registerExecutorCompatibilityGlobals(lua_State* L)
{
    registerFunction(L, LUA_GLOBALSINDEX, "identifyexecutor", identifyExecutor);
    registerFunction(L, LUA_GLOBALSINDEX, "getexecutorname", getExecutorName);
    registerFunction(L, LUA_GLOBALSINDEX, "getgenv", getGlobalEnvironment);
    registerFunction(L, LUA_GLOBALSINDEX, "getrenv", getGlobalEnvironment);
    registerFunction(L, LUA_GLOBALSINDEX, "getsenv", getScriptEnvironment);
    registerFunction(L, LUA_GLOBALSINDEX, "getrawmetatable", getRawMetatable);
    registerFunction(L, LUA_GLOBALSINDEX, "setrawmetatable", setRawMetatable);
    registerFunction(L, LUA_GLOBALSINDEX, "setreadonly", setReadonly);
    registerFunction(L, LUA_GLOBALSINDEX, "isreadonly", isReadonly);
    registerFunction(L, LUA_GLOBALSINDEX, "make_writeable", makeWriteable);
    registerFunction(L, LUA_GLOBALSINDEX, "makewriteable", makeWriteable);

    registerFunction(L, LUA_GLOBALSINDEX, "hookfunction", hookfunctionImpl);
    registerFunction(L, LUA_GLOBALSINDEX, "hookfunc", hookfunctionImpl); // Synapse X alias
    registerFunction(L, LUA_GLOBALSINDEX, "hookmetamethod", hookmetamethodImpl);
    registerFunction(L, LUA_GLOBALSINDEX, "getnamecallmethod", getNamecallMethodImpl);
    registerFunction(L, LUA_GLOBALSINDEX, "setnamecallmethod", setNamecallMethodImpl);

    registerFunction(L, LUA_GLOBALSINDEX, "getscriptbytecode", compileScriptBytecode);
    registerFunction(L, LUA_GLOBALSINDEX, "decompile", decompileScript);
    registerFunction(L, LUA_GLOBALSINDEX, "getscriptclosure", getScriptClosure);
    registerFunction(L, LUA_GLOBALSINDEX, "getupvalues", getUpvalues);
    registerFunction(L, LUA_GLOBALSINDEX, "getconstants", getconstantsImpl);
    registerFunction(L, LUA_GLOBALSINDEX, "getprotos", getprotosImpl);
    registerFunction(L, LUA_GLOBALSINDEX, "getcallbackvalue", getCallbackValue);

    registerFunction(L, LUA_GLOBALSINDEX, "getinstances", getInstances);
    registerFunction(L, LUA_GLOBALSINDEX, "getnilinstances", getNilInstances);
    registerFunction(L, LUA_GLOBALSINDEX, "getloadedmodules", getLoadedModules);
    registerFunction(L, LUA_GLOBALSINDEX, "getconnections", getConnections);
    registerFunction(L, LUA_GLOBALSINDEX, "firesignal", fireSignal);
    registerFunction(L, LUA_GLOBALSINDEX, "fireclickdetector", fireClickDetector);

    registerFunction(L, LUA_GLOBALSINDEX, "request", httpRequest);
    registerFunction(L, LUA_GLOBALSINDEX, "http_request", httpRequest);
    lua_getglobal(L, "http");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    const int http = lua_gettop(L);
    registerFunction(L, http, "request", httpRequest);
    registerFunction(L, http, "get", httpGet);
    registerFunction(L, http, "post", httpPost);
    lua_setglobal(L, "http");
    lua_getglobal(L, "syn");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    const int syn = lua_gettop(L);
    registerFunction(L, syn, "request", httpRequest);
    lua_setglobal(L, "syn");
    registerFunction(L, LUA_GLOBALSINDEX, "lz4compress", lz4Compress);
    registerFunction(L, LUA_GLOBALSINDEX, "lz4decompress", lz4Decompress);

    registerFunction(L, LUA_GLOBALSINDEX, "getthreadidentity", getThreadIdentity);
    registerFunction(L, LUA_GLOBALSINDEX, "setthreadidentity", setThreadIdentity);
    registerFunction(L, LUA_GLOBALSINDEX, "getidentity", getThreadIdentity);
    registerFunction(L, LUA_GLOBALSINDEX, "setidentity", setThreadIdentity);
    registerFunction(L, LUA_GLOBALSINDEX, "setfpscap", setFpsCap);
    registerFunction(L, LUA_GLOBALSINDEX, "getfpscap", getFpsCap);
    registerFunction(L, LUA_GLOBALSINDEX, "cloneref", cloneReference);
    registerFunction(L, LUA_GLOBALSINDEX, "clonereference", cloneReference);
    registerFunction(L, LUA_GLOBALSINDEX, "compareinstances", compareInstances);
    registerFunction(L, LUA_GLOBALSINDEX, "gethiddenproperty", getHiddenProperty);
    registerFunction(L, LUA_GLOBALSINDEX, "sethiddenproperty", setHiddenProperty);
    registerFunction(L, LUA_GLOBALSINDEX, "getrunningscripts", getRunningScripts);
    registerFunction(L, LUA_GLOBALSINDEX, "getscripthash", getScriptHash);
    registerFunction(L, LUA_GLOBALSINDEX, "getfunctionhash", getFunctionHash);
    registerFunction(L, LUA_GLOBALSINDEX, "isexecutorclosure", isExecutorClosure);
}

void registerRuntimeScript(lua_State* L, const char* path, const std::string& source)
{
    if (!makeInstance(L, "Script"))
        return;

    const int script = lua_gettop(L);
    setInstanceProperty(L, script, "Name", path ? path : "Script");
    setInternalSource(L, script, source);
    setInstanceParent(L, script, "ServerScriptService");

    lua_pushvalue(L, script);
    lua_setglobal(L, "script");
    appendLumoraArray(L, "_runningScripts", script);
    lua_pop(L, 1);
}

void recordLoadedModule(lua_State* L, const std::string& name, const std::string& source)
{
    const int base = lua_gettop(L);
    if (!makeInstance(L, "ModuleScript"))
        return;

    const int module = lua_gettop(L);
    setInstanceProperty(L, module, "Name", name);
    setInternalSource(L, module, source);
    setInstanceParent(L, module, "ReplicatedStorage");
    appendLumoraArray(L, "_loadedModules", module);
    lua_settop(L, base);
}

int lumoraFpsCap()
{
    return g_fpsCap.load();
}
