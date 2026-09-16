#include "lua.h"
#include "lualib.h"
#include "Luau/Bytecode.h"
#include "Luau/Compiler.h"
#include "Luau/Require.h"
#include "Luau/VfsNavigator.h"
#include "lumora.h"
#include "native_modules.h"
#include "stdlib_stringext.inc"
#include "stdlib_system.inc"
#include "stdlib_tableext.inc"
#include "stdlib_time.inc"

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

static int lumoraCompile(lua_State* L)
{
    size_t size = 0;
    const char* source = luaL_checklstring(L, 1, &size);
    try
    {
        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 2;
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

static int lumoraLoad(lua_State* L)
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
            options.debugLevel = 2;
            bytecode = Luau::compile(std::string(sourceOrBytecode, size), options);
        }

        if (luau_load(L, "=@lumora-runtime", bytecode.data(), bytecode.size(), 0) != 0)
        {
            const char* error = lua_tostring(L, -1);
            const std::string message = error ? error : "Luau load failed";
            lua_settop(L, 0);
            lua_pushnil(L);
            lua_pushstring(L, message.c_str());
            return 2;
        }

        // upstream runtime accepts { environment = table } as the second argument. The
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

static int lumoraStdioWrite(lua_State* L)
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

static int hostProcessExit(lua_State* L)
{
    const int code = int(luaL_optinteger(L, 1, 0));
    luaL_error(L, "process.exit(%d)", code);
    return 0;
}

static const char* embeddedModule(const char* name)
{
    if (!name) return nullptr;
    if (strcmp(name, "@lumora/fs") == 0)
        return "return lumora.fs\n";
    if (strcmp(name, "@lumora/stdio") == 0)
        return "return {write=function(...) io.write(...) end,print=print,readLine=function() return io.read(\"*l\") end} \n";
    if (strcmp(name, "@lumora/process") == 0)
        return "local h=__lumora_host; local p={}; p.args=arg; p.cwd=h.cwd; p.setCwd=h.setCwd; p.env=h.env; p.exec=h.exec; p.run=h.exec; p.spawn=h.exec; p.pid=h.pid; p.execPath=h.execPath; p.kill=h.kill; p.exit=function(code) error({__lumora_process_exit=code or 0}) end; return p\n";
    if (strcmp(name, "@lumora/luau") == 0)
        return R"LUMORA(local M = {}
M.load = loadstring
M.compile = function(source) return loadstring(source) end
function M.resolveModule(path, fromchunkname)
    assert(type(path) == "string", "module path must be a string")
    if path:sub(1, 8) == "@lumora/" then return path end
    assert(type(fromchunkname) == "string", "fromchunkname must be a string")
    local root = fromchunkname:gsub("^@", "")
    local wanted = path:gsub("^%./", "")
    local graph = lumora.analysis.graph("@file:" .. root)
    for _, edge in ipairs(graph.moduleEdges) do
        if edge.from == root or edge.from:sub(-#root) == root then
            if edge.to == path or edge.to:sub(-#wanted) == wanted or edge.to:sub(-#wanted - 5) == wanted .. ".luau" or edge.to:sub(-#wanted - 4) == wanted .. ".lua" then return edge.to end
        end
    end
    return nil
end
function M.typeofModule(path, fromchunkname)
    local resolved = M.resolveModule(path, fromchunkname)
    if not resolved then return nil end
    local ok, source = pcall(__lumora_host.readFile, resolved)
    if not ok then return nil end
    local expression = source:match("return%s+(.+)") or ""
    if expression:match("^function") or expression:match("^function%s*%(") then return "function" end
    if expression:match("^%{") then return "table" end
    if expression:match('^[' .. '"' .. "']") then return "string" end
    if expression:match("^%-?%d") then return "number" end
    if expression:match("^true") or expression:match("^false") then return "boolean" end
    return "unknown"
end
return M
)LUMORA";
    if (strcmp(name, "@lumora/datetime") == 0)
        return "local M={}\nlocal function t(sec, utc) return os.date(utc and '!%Y-%m-%dT%H:%M:%SZ' or '%Y-%m-%dT%H:%M:%S', sec) end\nfunction M.now() return os.time() end\nfunction M.fromUnix(ts) return os.date('!*t', ts) end\nfunction M.fromUnixTimestamp(ts) return M.fromUnix(ts) end\nfunction M.fromLocalTime(t0) return os.time(t0) end\nfunction M.fromUniversalTime(t0) return os.time(t0) end\nfunction M.toUnix(t0) return type(t0)=='number' and t0 or os.time(t0) end\nfunction M.fromIsoDate(s) local y,mo,d,h,mi,se=s:match('^(%d%d%d%d)%-(%d%d)%-(%d%d)[T ](%d%d):(%d%d):(%d%d)'); assert(y,'invalid ISO date'); return os.time({year=tonumber(y),month=tonumber(mo),day=tonumber(d),hour=tonumber(h),min=tonumber(mi),sec=tonumber(se)}) end\nM.fromRfc3339=M.fromIsoDate\nfunction M.fromRfc2822(s) local d,mon,y,h,mi,se=s:match('(%d%d?) (%a%a%a) (%d%d%d%d) (%d%d):(%d%d):(%d%d)'); local months={Jan=1,Feb=2,Mar=3,Apr=4,May=5,Jun=6,Jul=7,Aug=8,Sep=9,Oct=10,Nov=11,Dec=12}; assert(d and months[mon],'invalid RFC2822 date'); return os.time({year=tonumber(y),month=months[mon],day=tonumber(d),hour=tonumber(h),min=tonumber(mi),sec=tonumber(se)}) end\nfunction M.format(ts,fmt) return os.date(fmt or '!%Y-%m-%dT%H:%M:%SZ', ts or os.time()) end\nfunction M.toIsoDate(ts) return t(ts or os.time(),true) end\nreturn M\n";
    if (strcmp(name, "@lumora/serde") == 0)
        return R"LUMORA(local M = {}
M.encode = function(value) return json.encode(value) end
M.decode = function(value) return json.decode(value) end
M.hash = function(value) local s=json.encode(value); local h=0; for i=1,#s do h=(h*31+s:byte(i))%4294967296 end; return string.format('%08x',h) end
function M.tomlDecode(src)
    local out, current = {}, nil
    for line in src:gmatch('[^\n]+') do
        line = line:gsub('%s*#.*$', ''):gsub('^%s+', ''):gsub('%s+$', '')
        if line ~= '' then
            local section = line:match('^%[([^%]]+)%]$')
            if section then current = out; for part in section:gmatch('[^%.]+') do current[part] = current[part] or {}; current = current[part] end
            else
                local k, v = line:match('^([%w_%-]+)%s*=%s*(.-)%s*$'); assert(k, 'invalid TOML line: '..line)
                if v == 'true' then v=true elseif v == 'false' then v=false elseif v:sub(1,1) == '"' and v:sub(-1) == '"' then v=v:sub(2,-2) elseif v:sub(1,1) == '[' then local a={}; for item in v:sub(2,-2):gmatch('[^,]+') do item=item:gsub('^%s+',''):gsub('%s+$',''); if item:sub(1,1)=='"' then item=item:sub(2,-2) elseif item=='true' then item=true elseif item=='false' then item=false else item=tonumber(item) end; table.insert(a,item) end; v=a else v=tonumber(v) or v end
                (current or out)[k] = v
            end
        end
    end
    return out
end
function M.tomlEncode(t) local lines={}; for k,v in pairs(t) do if type(v)~='table' then local x=type(v)=='string' and string.format('%q',v) or tostring(v); table.insert(lines,k..' = '..x) end end; return table.concat(lines,'\n')..'\n' end
return M
)LUMORA";
    if (strcmp(name, "@lumora/task") == 0)
        return "return {spawn=task.spawn,defer=task.defer,delay=task.delay,cancel=task.cancel,wait=task.wait,resume=task.resume,status=task.status}\n";
    if (strcmp(name, "@lumora/stringext") == 0) return kLumoraStringext;
    if (strcmp(name, "@lumora/tableext") == 0) return kLumoraTableext;
    if (strcmp(name, "@lumora/time") == 0) return kLumoraTime;
    if (strcmp(name, "@lumora/system") == 0) return kLumoraSystem;
    if (strcmp(name, "@lumora/regex") == 0) return "return lumora.regex\n";
    if (strcmp(name, "@lumora/analysis") == 0) return "return lumora.analysis\n";
    if (strcmp(name, "@lumora/roblox") == 0)
        return "return {game=game,workspace=workspace,Instance=Instance,Enum=Enum,Vector2=Vector2,Vector3=Vector3,CFrame=CFrame,Color3=Color3,UDim=UDim,UDim2=UDim2,Ray=Ray,Random=Random}\n";
    return nullptr;
}

static void copyHostField(lua_State* L, int table, const char* name)
{
    lua_getglobal(L, "__lumora_host");
    lua_getfield(L, -1, name);
    lua_setfield(L, table, name);
    lua_pop(L, 1);
}

static int embeddedRequire(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    if (requireNativeModule(L, name))
        return 1;
    if (embeddedModule(name))
    {
        if (strncmp(name, "@lumora/", 8) == 0)
        {
            const char* source = embeddedModule(name);
            Luau::CompileOptions options;
            options.optimizationLevel = 1;
            options.debugLevel = 2;
            const std::string bytecode = Luau::compile(source, options);
            if (luau_load(L, name, bytecode.data(), bytecode.size(), 0) != 0)
                lua_error(L);
            if (lua_pcall(L, 0, 1, 0) != 0)
                lua_error(L);
            return 1;
        }
        lua_newtable(L); const int module = lua_gettop(L);
        if (strcmp(name, "@lumora/fs") == 0)
        {
            lua_getglobal(L, "lumora"); lua_getfield(L, -1, "fs");
            const char* fields[] = {"readFile", "writeFile", "appendFile", "readDir", "writeDir", "makeDir", "removeFile", "removeDir", "remove", "isFile", "isDir", "metadata", "copy", "move", "listDir", "link", "symlink", nullptr};
            for (int i = 0; fields[i]; ++i) { lua_getfield(L, -1, fields[i]); lua_setfield(L, module, fields[i]); }
            lua_pop(L, 2);
        }
        else if (strcmp(name, "@lumora/fs") == 0)
        {
            const char* fields[] = {"readFile", "writeFile", "exists", "isFile", "isDir", "readDir", "makeDir", "remove", "copy", "move", nullptr};
            for (int i = 0; fields[i]; ++i) copyHostField(L, module, fields[i]);
        }
        else if (strcmp(name, "@lumora/process") == 0)
        {
            copyHostField(L, module, "cwd"); copyHostField(L, module, "setCwd"); copyHostField(L, module, "env"); copyHostField(L, module, "exec"); copyHostField(L, module, "pid"); copyHostField(L, module, "execPath"); copyHostField(L, module, "kill");
            lua_getglobal(L, "arg"); lua_setfield(L, module, "args");
            addFunction(L, module, "exit", hostProcessExit);
        }
        else if (strcmp(name, "@lumora/luau") == 0)
        {
            addFunction(L, module, "load", lumoraLoad);
            addFunction(L, module, "compile", lumoraCompile);
        }
        else
        {
            lua_getglobal(L, "print"); lua_setfield(L, module, "print");
            addFunction(L, module, "write", lumoraStdioWrite);
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
    options.debugLevel = 2;
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
    // Keep a real ModuleScript-shaped record only after the module compiled,
    // ran, and returned exactly one value. Failed module attempts are not
    // reported as loaded state.
    recordLoadedModule(L, loadName ? loadName : chunkName ? chunkName : "ModuleScript", source);
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
