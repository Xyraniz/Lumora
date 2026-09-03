#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static std::string readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("cannot open script: ") + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static void pushArgs(lua_State* L, int argc, char** argv, int scriptIndex)
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

static const char* kRobloxPrelude = R"LUA(
local function signal()
    local s = { _connections = {} }
    function s:Connect(fn)
        assert(type(fn) == "function", "missing argument #1 to 'Connect' (function expected)")
        local c = { Connected = true }
        function c:Disconnect() self.Connected = false end
        table.insert(s._connections, { fn = fn, c = c })
        return c
    end
    function s:Fire(...)
        for _, x in ipairs(s._connections) do if x.c.Connected then x.fn(...) end end
    end
    return s
end

local instance_mt = {}
instance_mt.__index = function(self, key)
    if key == "GetChildren" then return function(obj) return obj._children end end
    if key == "FindFirstChild" then return function(obj, name) for _, c in ipairs(obj._children) do if c.Name == name then return c end end return nil end end
    if key == "WaitForChild" then return function(obj, name) return obj:FindFirstChild(name) or error("Infinite yield possible on '" .. obj:GetFullName() .. ":WaitForChild(" .. name .. ")'") end end
    if key == "GetService" then return function(obj, name) return obj._services[name] or Instance.new(name, obj) end end
    if key == "GetFullName" then return function(obj) local p=obj.Name; local q=obj.Parent; while q do p=q.Name.."."..p; q=q.Parent end return p end end
    if key == "Destroy" then return function(obj) obj.Parent=nil end end
    if key == "IsA" then return function(obj, n) return obj.ClassName == n or n == "Instance" end end
    if key == "GetAttribute" then return function(obj, n) return obj._attributes[n] end end
    if key == "SetAttribute" then return function(obj, n, v) obj._attributes[n]=v; obj.AttributeChanged:Fire(n) end end
    return rawget(self, key)
end
instance_mt.__newindex = function(self, key, value)
    rawset(self, key, value)
    if key == "Parent" and value then table.insert(value._children, self); value.ChildAdded:Fire(self) end
end

Instance = {}
function Instance.new(className, parent)
    local o = setmetatable({ ClassName=className, Name=className, Parent=nil, _children={}, _attributes={}, AttributeChanged=signal(), ChildAdded=signal(), ChildRemoved=signal() }, instance_mt)
    if parent then o.Parent = parent end
    return o
end

local root = Instance.new("DataModel")
root.Name = "game"
root._services = {}
function root:GetService(name) if not root._services[name] then root._services[name]=Instance.new(name, root) end return root._services[name] end
game = root
workspace = root:GetService("Workspace")
workspace.Name = "Workspace"

Enum = setmetatable({}, { __index = function(_, enumName)
    local t = { Name=enumName }
    setmetatable(t, { __index = function(e, item) return { Name=item, EnumType=e, Value=0, __type="EnumItem" } end, __tostring=function() return "Enum."..enumName end })
    return t
end })

local function vec2(x,y) return setmetatable({X=x or 0,Y=y or 0}, {__index={Magnitude=math.sqrt((x or 0)^2+(y or 0)^2)}, __tostring=function(v) return string.format("%g, %g",v.X,v.Y) end}) end
Vector2 = { new=vec2 }
UDim2 = { new=function(sx,ox,sy,oy) return {X={Scale=sx,Offset=ox},Y={Scale=sy,Offset=oy}} end, fromScale=function(x,y) return UDim2.new(x,0,y,0) end, fromOffset=function(x,y) return UDim2.new(0,x,0,y) end }
Path2D = { new=function() return {ControlPoints={}} end }

Random = {}
function Random.new(seed)
    local state = (seed or os.time()) % 4294967296
    local r = {}
    local function next32() state = (1664525 * state + 1013904223) % 4294967296; return state end
    function r:NextInteger(a,b) return a + (next32() % (b-a+1)) end
    function r:NextNumber(a,b) a=a or 0; b=b or 1; return a + (next32()/4294967296)*(b-a) end
    function r:NextUnitVector() local a=self:NextNumber(0, math.pi*2); return vec2(math.cos(a),math.sin(a)) end
    return r
end

local tasklib = {}
function tasklib.spawn(fn, ...) local co=coroutine.create(fn); local ok,err=coroutine.resume(co,...); if not ok then error(err,0) end; return co end
function tasklib.delay(_, fn, ...) return tasklib.spawn(fn, ...) end
function tasklib.cancel(_) end
function tasklib.wait(seconds) return seconds or 0 end
task = tasklib

typeof = function(v) if type(v)=="table" and v.ClassName then return "Instance" elseif type(v)=="table" and v.__type then return v.__type elseif type(v)=="table" and v.X and v.Y then return "Vector2" else return type(v) end end
iscclosure = function(f) return type(f)=="function" end
islclosure = iscclosure
newcclosure = function(f) assert(type(f)=="function", "function expected"); return f end
clonefunction = function(f) assert(type(f)=="function", "function expected"); return f end
getfenv = function(_) return _ENV end
setfenv = function(_, env) return env end
if not table.freeze then table.freeze=function(t) return t end end
)LUA";

static bool installPrelude(lua_State* L)
{
    Luau::CompileOptions options;
    const std::string bytecode = Luau::compile(kRobloxPrelude, options);
    if (luau_load(L, "=lumora.roblox", bytecode.data(), bytecode.size(), 0) != 0)
        return false;
    return lua_pcall(L, 0, 0, 0) == 0;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: luau-vm script.lua [args...]\n";
        return 2;
    }

    try
    {
        const std::string source = readFile(argv[1]);
        lua_State* L = luaL_newstate();
        if (!L)
        {
            std::cerr << "failed to create Luau state\n";
            return 70;
        }
        luaL_openlibs(L);
        pushArgs(L, argc, argv, 1);
        if (!installPrelude(L))
        {
            std::cerr << lua_tostring(L, -1) << "\n";
            lua_close(L);
            return 70;
        }

        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        const std::string bytecode = Luau::compile(source, options);
        if (luau_load(L, argv[1], bytecode.data(), bytecode.size(), 0) != 0)
        {
            std::cerr << lua_tostring(L, -1) << "\n";
            lua_close(L);
            return 1;
        }
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
        {
            std::cerr << lua_tostring(L, -1) << "\n";
            lua_close(L);
            return 1;
        }
        lua_close(L);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 2;
    }
}
