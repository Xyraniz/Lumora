#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <cstring>
#include <string>

// The Lua prelude is split by responsibility into adjacent raw string
// fragments. The compiler concatenates them into one unchanged chunk, so
// initialization order and runtime behavior remain exactly the same.
static const char* kRobloxPrelude =
#include "prelude_core.inc"
#include "prelude_windui.inc"
#include "prelude_icons.inc"
#include "prelude_services.inc"
;

// loadstring(source [, chunkName]) — mirrors the Roblox global. Compiles
// Luau source to bytecode and loads it as a function. Returns (fn) on
// success or (nil, errorMessage) on a compile error, matching the contract
// that general Roblox scripts rely on.
static int lumora_loadstring(lua_State* L)
{
    const char* source = luaL_optstring(L, 1, "");
    const char* chunkName = luaL_optstring(L, 2, "loadstring");

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 2;

    std::string bytecode;
    try
    {
        bytecode = Luau::compile(source, options);
    }
    catch (const std::exception&)
    {
        lua_pushnil(L);
        lua_pushstring(L, "loadstring: compilation failed");
        return 2;
    }

    // Luau::compile returns bytecode even on success, but a compile error
    // is encoded as a #0 directive inside the bytecode. luau_load detects
    // that and pushes an error string on the stack.
    std::string chunkId = std::string("=") + chunkName;
    if (luau_load(L, chunkId.c_str(), bytecode.data(), bytecode.size(), 0) != 0)
    {
        // luau_load already pushed an error string; replace it under nil
        const char* err = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, err ? err : "loadstring: load failed");
        return 2;
    }

    return 1;
}

bool installPrelude(lua_State* L)
{
    Luau::CompileOptions options;
    const std::string bytecode = Luau::compile(kRobloxPrelude, options);
    if (luau_load(L, "=lumora.roblox", bytecode.data(), bytecode.size(), 0) != 0)
        return false;
    return lua_pcall(L, 0, 0, 0) == 0;
}

// Register native globals that Luau's base library intentionally omits
// (loadstring, load) but that Roblox provides. Must be called after
// luaL_openlibs and before the user script runs.

// iscclosure(f) -> bool : true when f is a C closure, false for Lua closures.
static int lumora_iscclosure(lua_State* L)
{
    luaL_checkany(L, 1);
    lua_pushboolean(L, lua_iscfunction(L, 1));
    return 1;
}

// islclosure(f) -> bool : the complement of iscclosure.
static int lumora_islclosure(lua_State* L)
{
    luaL_checkany(L, 1);
    lua_pushboolean(L, !lua_iscfunction(L, 1) && lua_isfunction(L, 1));
    return 1;
}

// newcclosure(f) -> cFunction : wraps a Lua function in a C closure so that
// iscclosure reports true. The upvalue at slot 1 carries the original fn.
static int lumora_newcclosure_thunk(lua_State* L)
{
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static int lumora_newcclosure(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushcclosure(L, lumora_newcclosure_thunk, "newcclosure", 1);
    return 1;
}

// clonefunction(f) -> function : returns a function with identical behaviour.
// For C closures we return the same reference (they are immutable). For Lua
// closures we also return the same reference — Luau does not expose a
// bytecode-level clone, and identity is sufficient for the Roblox
// compatibility contract that clonefunction(pcall) still works.
static int lumora_clonefunction(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    return 1;
}

// Set of __type markers that Roblox reports as "userdata" for type().
// Everything else carrying a __type marker is a value type (Vector3, Color3,
// CFrame, ...) and type() reports "userdata" for those too in real Roblox,
// but we keep type() aligned with Luau's native semantics for plain tables
// while typeof() exposes the Roblox type name.
static bool isRobloxUserdata(const char* marker)
{
    return strcmp(marker, "Instance") == 0 || strcmp(marker, "Enums") == 0 ||
           strcmp(marker, "Enum") == 0 || strcmp(marker, "EnumItem") == 0;
}

// Set of __type markers that name a Roblox value type. typeof() returns the
// marker verbatim for these so scripts get "Vector3", "Color3", "CFrame",
// "UDim2", "Ray", etc. exactly as Roblox does.
static bool isRobloxValueType(const char* marker)
{
    static const char* kValueTypes[] = {
        "Vector2", "Vector3", "Color3", "CFrame", "UDim", "UDim2", "Ray",
        "RaycastParams", "NumberRange", "NumberSequence", "NumberSequenceKeypoint",
        "ColorSequence", "ColorSequenceKeypoint", "BrickColor", "TweenInfo",
        "Font", "Rect", "Path2D", "PhysicalProperties", "Enums", "Enum",
        "EnumItem", "Instance", "Random", "Drawing", "WindUIElement", "Tween",
        nullptr};
    for (int i = 0; kValueTypes[i]; ++i)
        if (strcmp(marker, kValueTypes[i]) == 0) return true;
    return false;
}

// Roblox-aware type(v) -> string. Identical to native Luau type() except that
// tables carrying a __type marker of "Instance", "Enums", "Enum", or
// "EnumItem" report "userdata", matching what real Roblox returns for its
// native objects. Implemented as a C closure so that builtins behave
// consistently with Roblox's own C-closure implementation.
static int lumora_type(lua_State* L)
{
    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TTABLE)
    {
        // Use rawget to avoid triggering __index metamethods on emulated
        // userdata metatables (e.g. Enum's __index creates enum types).
        lua_pushstring(L, "__type");
        lua_rawget(L, 1);
        if (lua_isstring(L, -1))
        {
            const char* marker = lua_tostring(L, -1);
            // Roblox reports "userdata" for Instance-like objects AND for all
            // the value datatypes (Vector3, Color3, CFrame, ...). This keeps
            // type() consistent with real Roblox behavior.
            if (isRobloxUserdata(marker) || isRobloxValueType(marker))
            {
                if (isRobloxUserdata(marker))
                {
                    lua_pop(L, 1);
                    lua_pushstring(L, "userdata");
                    return 1;
                }
                // Value datatypes report "userdata" in Roblox's type() too.
                lua_pop(L, 1);
                lua_pushstring(L, "userdata");
                return 1;
            }
        }
        lua_pop(L, 1);
    }
    lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
    return 1;
}

// Roblox-aware typeof(v) -> string. Returns the __type marker verbatim for
// all emulated Roblox value/userdata types so scripts get "Vector3",
// "Color3", "CFrame", "Instance", "Enum", "UDim2", "Ray", etc. exactly as
// Roblox does. Falls back to native Luau type() otherwise.
static int lumora_typeof(lua_State* L)
{
    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TTABLE)
    {
        // Check __type marker first (covers all emulated datatypes and
        // Instances which carry __type="Instance"). Use rawget to avoid
        // triggering __index metamethods on emulated userdata metatables.
        lua_pushstring(L, "__type");
        lua_rawget(L, 1);
        if (lua_isstring(L, -1))
        {
            const char* marker = lua_tostring(L, -1);
            if (isRobloxValueType(marker))
            {
                // Return the marker verbatim (it's already on the stack).
                return 1;
            }
        }
        lua_pop(L, 1);

        // Fallback for plain tables that look like an Instance (have a
        // ClassName field) but no __type marker — return "Instance".
        lua_pushstring(L, "ClassName");
        lua_rawget(L, 1);
        if (!lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_pushstring(L, "Instance");
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
    return 1;
}

void registerRobloxGlobals(lua_State* L)
{
    lua_pushcfunction(L, lumora_loadstring, "loadstring");
    lua_setglobal(L, "loadstring");

    // load(source [, chunkName [, env]]) — Luau variant. We support the
    // first two arguments and compile the same way as loadstring.
    lua_pushcfunction(L, lumora_loadstring, "load");
    lua_setglobal(L, "load");

    lua_pushcfunction(L, lumora_iscclosure, "iscclosure");
    lua_setglobal(L, "iscclosure");

    lua_pushcfunction(L, lumora_islclosure, "islclosure");
    lua_setglobal(L, "islclosure");

    lua_pushcfunction(L, lumora_newcclosure, "newcclosure");
    lua_setglobal(L, "newcclosure");

    lua_pushcfunction(L, lumora_clonefunction, "clonefunction");
    lua_setglobal(L, "clonefunction");

    // Roblox-aware type/typeof implemented as C closures so that builtins
    // behave consistently with Roblox's own C-closure implementation.
    lua_pushcfunction(L, lumora_type, "type");
    lua_setglobal(L, "type");

    lua_pushcfunction(L, lumora_typeof, "typeof");
    lua_setglobal(L, "typeof");

    // Note: readfile/isfile/loadfile remain as the in-memory stubs defined in
    // the prelude. Lumora deliberately does NOT expose real filesystem access
    // to scripts, so untrusted code cannot read host files. WindUI and other
    // vendored libraries are served from the embedded prelude instead of disk.
}
