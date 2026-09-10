#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "Luau/Require.h"
#include "Luau/VfsNavigator.h"
#include "lumora.h"

#include <fstream>
#include <cstring>
#include <new>
#include <sstream>
#include <string>

namespace
{
struct RequireContext
{
    VfsNavigator vfs;
};

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
    return writeString("@" + static_cast<RequireContext*>(ctx)->vfs.getFilePath(), buffer, bufferSize, sizeOut);
}

static luarequire_WriteResult getLoadName(lua_State* /*L*/, void* ctx, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    return writeString(static_cast<RequireContext*>(ctx)->vfs.getAbsoluteFilePath(), buffer, bufferSize, sizeOut);
}

static luarequire_WriteResult getCacheKey(lua_State* /*L*/, void* ctx, char* buffer, size_t bufferSize, size_t* sizeOut)
{
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
    (void)context;
    const std::string source = readModule(loadName);
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
}
