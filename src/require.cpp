#include "lua.h"
#include "lualib.h"
#include "Luau/Bytecode.h"
#include "Luau/Compiler.h"
#include "Luau/Require.h"
#include "Luau/VfsNavigator.h"
#include "lumora.h"

#include <fstream>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>
#include <string>

namespace
{
struct RequireContext
{
    VfsNavigator vfs;
    std::string embedded;
};

static bool looksLikeBytecode(const char* data, size_t size)
{
    if (size == 0)
        return false;
    const unsigned char version = static_cast<unsigned char>(data[0]);
    return version == 0 ||
        (version >= LBC_VERSION_MIN && version <= LBC_VERSION_MAX) ||
        version == LBC_VERSION_CLASSES;
}

static void addFunction(lua_State* L, int table, const char* name, lua_CFunction function)
{
    lua_pushcfunction(L, function, name);
    lua_setfield(L, table, name);
}

static int luneCompile(lua_State* L)
{
    size_t size = 0;
    const char* source = luaL_checklstring(L, 1, &size);
    try
    {
        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        const std::string bytecode = Luau::compile(std::string(source, size), options);
        lua_pushlstring(L, bytecode.data(), bytecode.size());
        return 1;
    }
    catch (const std::exception& error)
    {
        lua_pushnil(L);
        lua_pushstring(L, error.what());
        return 2;
    }
}

static int luneLoad(lua_State* L)
{
    size_t size = 0;
    const char* sourceOrBytecode = luaL_checklstring(L, 1, &size);
    std::string bytecode;
    try
    {
        if (looksLikeBytecode(sourceOrBytecode, size))
            bytecode.assign(sourceOrBytecode, size);
        else
        {
            Luau::CompileOptions options;
            options.optimizationLevel = 1;
            options.debugLevel = 1;
            bytecode = Luau::compile(std::string(sourceOrBytecode, size), options);
        }

        if (luau_load(L, "=@lumora-lune-compat", bytecode.data(), bytecode.size(), 0) != 0)
        {
            const char* error = lua_tostring(L, -1);
            const std::string message = error ? error : "Luau load failed";
            lua_settop(L, 0);
            lua_pushnil(L);
            lua_pushstring(L, message.c_str());
            return 2;
        }

        // Lune accepts { environment = table } as the second argument. The
        // generated safe runners use this to keep recovered code in a closed
        // lexical environment instead of the host globals.
        const int functionIndex = lua_gettop(L);
        if (lua_istable(L, 2))
        {
            lua_getfield(L, 2, "environment");
            if (lua_istable(L, -1))
                lua_setfenv(L, functionIndex);
            else
                lua_pop(L, 1);
        }
        return 1;
    }
    catch (const std::exception& error)
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        lua_pushstring(L, error.what());
        return 2;
    }
}

static int luneStdioWrite(lua_State* L)
{
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i)
    {
        size_t size = 0;
        const char* value = luaL_tolstring(L, i, &size);
        if (value)
            std::cout.write(value, std::streamsize(size));
        lua_pop(L, 1);
    }
    std::cout.flush();
    return 0;
}

static int luneProcessExit(lua_State* L)
{
    const int code = int(luaL_optinteger(L, 1, 0));
    luaL_error(L, "process.exit(%d)", code);
    return 0;
}

static const char* embeddedModule(const char* name)
{
    if (!name) return nullptr;
    if (strcmp(name, "@lune/fs") == 0)
        return "local h=__lumora_lune; return {readFile=h.readFile,writeFile=h.writeFile,exists=h.exists,readDir=h.readDir,makeDir=h.makeDir,remove=h.remove,move=function(a,b) local d=h.readFile(a); h.writeFile(b,d); h.remove(a) end,copy=function(a,b) h.writeFile(b,h.readFile(a)) end,isFile=function(p) return h.exists(p) and true or false end,isDir=function(p) return h.exists(p) and true or false end} \n";
    if (strcmp(name, "@lune/stdio") == 0)
        return "return {write=function(...) io.write(...) end,print=print,readLine=function() return io.read(\"*l\") end} \n";
    if (strcmp(name, "@lune/process") == 0)
        return "local p={}; p.args=arg; p.cwd=__lumora_lune.cwd; p.setCwd=__lumora_lune.setCwd; p.env=__lumora_lune.env; p.exit=function(code) error({__lumora_process_exit=code or 0}) end; return p\n";
    if (strcmp(name, "@lune/luau") == 0)
        return "return {load=loadstring,compile=function(source) return loadstring(source) end}\n";
    return nullptr;
}

static void copyHostField(lua_State* L, int table, const char* name)
{
    lua_getglobal(L, "__lumora_lune");
    lua_getfield(L, -1, name);
    lua_setfield(L, table, name);
    lua_pop(L, 1);
}

static int embeddedRequire(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    if (embeddedModule(name))
    {
        lua_newtable(L); const int module = lua_gettop(L);
        if (strcmp(name, "@lune/fs") == 0)
        {
            const char* fields[] = {"readFile", "writeFile", "exists", "readDir", "makeDir", "remove", nullptr};
            for (int i = 0; fields[i]; ++i) copyHostField(L, module, fields[i]);
        }
        else if (strcmp(name, "@lune/process") == 0)
        {
            copyHostField(L, module, "cwd"); copyHostField(L, module, "setCwd"); copyHostField(L, module, "env");
            lua_getglobal(L, "arg"); lua_setfield(L, module, "args");
            addFunction(L, module, "exit", luneProcessExit);
        }
        else if (strcmp(name, "@lune/luau") == 0)
        {
            addFunction(L, module, "load", luneLoad);
            addFunction(L, module, "compile", luneCompile);
        }
        else
        {
            lua_getglobal(L, "print"); lua_setfield(L, module, "print");
            addFunction(L, module, "write", luneStdioWrite);
            lua_getglobal(L, "print"); lua_setfield(L, module, "readLine");
        }
        return 1;
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 1, 1);
    return 1;
}

static luarequire_NavigateResult convert(NavigationStatus status)
{
    if (status == NavigationStatus::Success)
        return NAVIGATE_SUCCESS;
    if (status == NavigationStatus::Ambiguous)
        return NAVIGATE_AMBIGUOUS;
    return NAVIGATE_NOT_FOUND;
}

static void destroyRequireContext(void* value)
{
    static_cast<RequireContext*>(value)->~RequireContext();
}

static std::string readModule(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static bool requireAllowed(lua_State* /*L*/, void* /*ctx*/, const char* chunkName)
{
    // Only file-backed chunks may require another file.  This prevents a
    // dynamically-created loadstring chunk from silently changing the module
    // root and mirrors the restriction used by Luau's CLI loader.
    return chunkName && chunkName[0] == '@';
}

static luarequire_NavigateResult reset(lua_State* /*L*/, void* ctx, const char* chunkName)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    context->embedded.clear();
    if (chunkName && embeddedModule(chunkName))
    {
        context->embedded = chunkName;
        return NAVIGATE_SUCCESS;
    }
    return convert(context->vfs.resetToPath(chunkName && chunkName[0] == '@' ? chunkName + 1 : ""));
}

static luarequire_NavigateResult jumpToAlias(lua_State* /*L*/, void* ctx, const char* path)
{
    // Lumora does not interpret project aliases yet, but absolute paths are a
    // safe and useful fallback for configuration files that provide one.
    if (!path || (path[0] != '/' && !(path[0] && path[1] == ':')))
        return NAVIGATE_NOT_FOUND;

    RequireContext* context = static_cast<RequireContext*>(ctx);
    return convert(context->vfs.resetToPath(path));
}

static luarequire_NavigateResult toParent(lua_State* /*L*/, void* ctx)
{
    return convert(static_cast<RequireContext*>(ctx)->vfs.toParent());
}

static luarequire_NavigateResult toChild(lua_State* /*L*/, void* ctx, const char* name)
{
    return convert(static_cast<RequireContext*>(ctx)->vfs.toChild(name ? name : ""));
}

static bool modulePresent(lua_State* /*L*/, void* ctx)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    if (!context->embedded.empty()) return true;
    return !context->vfs.getFilePath().empty();
}

static luarequire_WriteResult writeString(const std::string& value, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    const size_t required = value.size() + 1;
    if (bufferSize < required)
    {
        *sizeOut = required;
        return WRITE_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, value.c_str(), required);
    *sizeOut = required;
    return WRITE_SUCCESS;
}

static luarequire_WriteResult getChunkName(lua_State* /*L*/, void* ctx, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    if (!context->embedded.empty()) return writeString(context->embedded, buffer, bufferSize, sizeOut);
    return writeString("@" + static_cast<RequireContext*>(ctx)->vfs.getFilePath(), buffer, bufferSize, sizeOut);
}

static luarequire_WriteResult getLoadName(lua_State* /*L*/, void* ctx, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    if (!context->embedded.empty()) return writeString(context->embedded, buffer, bufferSize, sizeOut);
    return writeString(static_cast<RequireContext*>(ctx)->vfs.getAbsoluteFilePath(), buffer, bufferSize, sizeOut);
}

static luarequire_WriteResult getCacheKey(lua_State* /*L*/, void* ctx, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    if (!context->embedded.empty()) return writeString(context->embedded, buffer, bufferSize, sizeOut);
    return writeString(static_cast<RequireContext*>(ctx)->vfs.getAbsoluteFilePath(), buffer, bufferSize, sizeOut);
}

static luarequire_ConfigStatus getConfigStatus(lua_State* /*L*/, void* /*ctx*/)
{
    // Configuration aliases are deliberately not enabled until Lumora can
    // apply them consistently to both --no-roblox and Roblox mode.
    return CONFIG_ABSENT;
}

static luarequire_WriteResult getConfig(lua_State* /*L*/, void* /*ctx*/, char* /*buffer*/, size_t /*bufferSize*/, size_t* /*sizeOut*/)
{
    return WRITE_FAILURE;
}

static int loadModule(lua_State* L, void* ctx, const char* /*path*/, const char* chunkName, const char* loadName)
{
    RequireContext* context = static_cast<RequireContext*>(ctx);
    const char* builtIn = embeddedModule(context->embedded.c_str());
    const std::string source = builtIn ? builtIn : readModule(loadName);
    if (source.empty() && !std::ifstream(loadName, std::ios::binary).good())
        luaL_error(L, "could not read module '%s'", loadName ? loadName : "");

    // A module gets its own coroutine, like the Luau CLI loader.  It shares
    // Lumora's global environment, so Roblox modules can see game/workspace,
    // while the module's return value is isolated and cached by require.  Do
    // not call luaL_sandboxthread here: that would hide the Roblox prelude
    // from ModuleScripts and make require behave differently in each mode.
    lua_State* main = lua_mainthread(L);
    lua_State* module = lua_newthread(main);
    lua_xmove(main, L, 1);

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    const std::string bytecode = Luau::compile(source, options);
    if (luau_load(module, chunkName, bytecode.data(), bytecode.size(), 0) != 0)
    {
        const char* error = lua_tostring(module, -1);
        luaL_error(L, "error while loading module '%s': %s", loadName, error ? error : "unknown error");
    }

    const int status = lua_resume(module, L, 0);
    if (status == LUA_YIELD)
        luaL_error(L, "module '%s' can not yield", loadName);
    if (status != 0)
    {
        const char* error = lua_tostring(module, -1);
        luaL_error(L, "error while running module '%s': %s", loadName, error ? error : "unknown error");
    }
    if (lua_gettop(module) != 1)
        luaL_error(L, "module '%s' must return a single value", loadName);

    lua_xmove(module, L, 1);
    lua_remove(L, -2); // remove the module thread kept alive by this call
    return 1;
}

void requireConfigInit(luarequire_Configuration* config)
{
    config->is_require_allowed = requireAllowed;
    config->reset = reset;
    config->jump_to_alias = jumpToAlias;
    config->to_parent = toParent;
    config->to_child = toChild;
    config->is_module_present = modulePresent;
    config->get_chunkname = getChunkName;
    config->get_loadname = getLoadName;
    config->get_cache_key = getCacheKey;
    config->get_config_status = getConfigStatus;
    config->get_config = getConfig;
    config->load = loadModule;
}
}

void registerRequire(lua_State* L)
{
    void* storage = lua_newuserdatadtor(L, sizeof(RequireContext), destroyRequireContext);
    new (storage) RequireContext{};

    // Keep the context alive as long as the state.  The require closure only
    // stores the light-userdata pointer, so the registry owns the object.
    lua_pushlightuserdata(L, storage);
    lua_insert(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);

    luaopen_require(L, requireConfigInit, storage);
    lua_getglobal(L, "require");
    lua_pushcclosure(L, embeddedRequire, "require", 1);
    lua_setglobal(L, "require");
}
