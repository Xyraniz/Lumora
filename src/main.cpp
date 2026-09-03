#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <cmath>
#if defined(__unix__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

local instance_mt = { __metatable = "The metatable is locked" }
local function removeChild(parent, child)
    for i, value in ipairs(parent._children) do
        if value == child then table.remove(parent._children, i); return end
    end
end
instance_mt.__index = function(self, key)
    if key == "Parent" then return rawget(self, "_parent") end
    if key == "GetChildren" then return function(obj) if obj == nil then error("Expected ':' not '.' calling member function GetChildren", 0) end return obj._children end end
    if key == "FindFirstChild" then return function(obj, name) for _, c in ipairs(obj._children) do if c.Name == name then return c end end return nil end end
    if key == "WaitForChild" then return function(obj, name) return obj:FindFirstChild(name) or error("Infinite yield possible on '" .. obj:GetFullName() .. ":WaitForChild(" .. name .. ")'") end end
    if key == "GetService" then return function(obj, name) return obj._services[name] or Instance.new(name, obj) end end
    if key == "GetFullName" then return function(obj) local p=obj.Name; local q=obj.Parent; while q do p=q.Name.."."..p; q=q.Parent end return p end end
    if key == "Destroy" then return function(obj)
        if obj.Parent then obj.Parent = nil end
        while #obj._children > 0 do obj._children[1]:Destroy() end
        obj._destroyed = true
    end end
    if key == "IsA" then return function(obj, n) return obj.ClassName == n or n == "Instance" end end
    if key == "GetAttribute" then return function(obj, n) return obj._attributes[n] end end
    if key == "SetAttribute" then return function(obj, n, v) obj._attributes[n]=v; obj.AttributeChanged:Fire(n) end end
    return rawget(self, key)
end
instance_mt.__newindex = function(self, key, value)
    if key == "Parent" then
        local old = rawget(self, "_parent")
        if old == value then return end
        if old then removeChild(old, self); old.ChildRemoved:Fire(self) end
        rawset(self, "_parent", value)
        if value then
            removeChild(value, self)
            table.insert(value._children, self)
            value.ChildAdded:Fire(self)
        end
        return
    end
    rawset(self, key, value)
end

Instance = {}
function Instance.new(className, parent)
    local o = setmetatable({ ClassName=className, Name=className, __type="Instance", _children={}, _attributes={}, AttributeChanged=signal(), ChildAdded=signal(), ChildRemoved=signal() }, instance_mt)
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

local function enumValue(name)
    local value = 0
    for i = 1, #name do value = (value * 33 + string.byte(name, i)) % 2147483647 end
    return value
end
Enum = setmetatable({ __type="Enums" }, { __metatable="The metatable is locked", __index = function(_, enumName)
    local t = { Name=enumName, __type="Enum", _items={} }
    rawset(_, enumName, t)
        function t:FromName(name) return self[name] end
    function t:FromValue(value) for _, item in ipairs(self._items) do if item.Value==value then return item end end return nil end
    setmetatable(t, { __metatable="The metatable is locked", __index = function(e, item) local v=setmetatable({ Name=item, EnumType=e, Value=#e._items, __type="EnumItem" }, { __tostring=function() return "Enum."..enumName.."."..item end }); rawset(e, item, v); table.insert(e._items, v); return v end, __tostring=function() return "Enum."..enumName end })
    return t
end })

local builtin_type = type
type = function(value)
    if builtin_type(value) == "table" then
        local marker = rawget(value, "__type")
        if marker == "Instance" or marker == "Enums" or marker == "Enum" or marker == "EnumItem" then return "userdata" end
    end
    return builtin_type(value)
end

local function vec2(x,y)
    return setmetatable({X=x or 0,Y=y or 0,__type="Vector2"}, {__index={Magnitude=math.sqrt((x or 0)^2+(y or 0)^2)}, __tostring=function(v) return string.format("%g, %g",v.X,v.Y) end})
end
Vector2 = { new=vec2 }
UDim2 = {
    new=function(sx,ox,sy,oy) return {X={Scale=sx,Offset=ox},Y={Scale=sy,Offset=oy},__type="UDim2"} end,
    fromScale=function(x,y) return UDim2.new(x,0,y,0) end,
    fromOffset=function(x,y) return UDim2.new(0,x,0,y) end
}
Path2D = { new=function() return {ControlPoints={},__type="Path2D"} end }

Random = {}
local function mul32(a, b)
    -- Split into 16-bit limbs so every intermediate remains exactly
    -- representable as a Luau double. This is the low-level operation used
    -- by the 64-bit PCG state below.
    local a0 = bit32.band(a, 65535)
    local a1 = math.floor(a / 65536)
    local b0 = bit32.band(b, 65535)
    local b1 = math.floor(b / 65536)
    local p0 = a0 * b0
    local p1 = a0 * b1 + a1 * b0
    local p2 = a1 * b1
    local t = math.floor(p0 / 65536) + bit32.band(p1, 65535)
    local lo = (bit32.band(p0, 65535) + bit32.band(t, 65535) * 65536) % 4294967296
    local hi = (math.floor(t / 65536) + math.floor(p1 / 65536) + p2) % 4294967296
    return lo, hi
end
local function pcgNext(s)
    local oldLo, oldHi = s.lo, s.hi
    local lo, carry = mul32(oldLo, 0x4c957f2d)
    local cross1 = mul32(oldLo, 0x5851f42d)
    local cross2 = mul32(oldHi, 0x4c957f2d)
    local highLo = (carry + cross1 + cross2) % 4294967296
    lo = (lo + 105) % 4294967296
    if lo < 105 then highLo = (highLo + 1) % 4294967296 end
    s.lo, s.hi = lo, highLo
    local shiftedLo = bit32.bor(bit32.rshift(oldLo, 18), bit32.lshift(bit32.band(oldHi, 0x3ffff), 14))
    local shiftedHi = bit32.rshift(oldHi, 18)
    local xlo = bit32.bxor(oldLo, shiftedLo)
    local xhi = bit32.bxor(oldHi, shiftedHi)
    local xorshifted = bit32.bor(bit32.rshift(xlo, 27), bit32.lshift(bit32.band(xhi, 0x7ffffff), 5))
    return bit32.rrotate(xorshifted, bit32.rshift(oldHi, 27))
end
function Random.new(seed)
    seed = math.floor(seed or os.time())
    local s = {lo=0, hi=0}
    pcgNext(s)
    s.lo = (s.lo + seed % 4294967296) % 4294967296
    s.hi = (s.hi + math.floor(seed / 4294967296)) % 4294967296
    pcgNext(s)
    local r = {_state=s}
    function r:NextInteger(a,b)
        a = assert(a, "missing argument #1 to 'NextInteger' (number expected)")
        b = assert(b, "missing argument #2 to 'NextInteger' (number expected)")
        local width = b - a + 1
        assert(width > 0, "interval is empty")
        return a + math.floor((pcgNext(self._state) / 4294967296) * width)
    end
    function r:NextNumber(a,b)
        a = a or 0; b = b or 1
        local lo, hi = pcgNext(self._state), pcgNext(self._state)
        local value = (hi * 4294967296 + lo) / 18446744073709551616
        return a + value * (b-a)
    end
    function r:NextUnitVector() local a=self:NextNumber(0, math.pi*2); return vec2(math.cos(a),math.sin(a)) end
    function r:Clone() local copy = Random.new(0); copy._state = {lo=self._state.lo, hi=self._state.hi}; return copy end
    return r
end

local tasklib = {}
function tasklib.spawn(fn, ...) local co=coroutine.create(fn); local ok,err=coroutine.resume(co,...); if not ok then error(err,0) end; return co end
function tasklib.delay(_, fn, ...)
    local co = coroutine.create(fn)
    local args = table.pack(...)
    tasklib._delayed = tasklib._delayed or {}
    tasklib._delayed[co] = args
    return co
end
function tasklib.cancel(co)
    if type(co) == "thread" then
        tasklib._delayed = tasklib._delayed or {}
        tasklib._delayed[co] = nil
        pcall(coroutine.close, co)
    end
end
function tasklib.wait(seconds) return seconds or 0 end
function tasklib.defer(fn, ...) return tasklib.spawn(fn, ...) end
 task = tasklib

typeof = function(v) if builtin_type(v)=="table" and rawget(v, "ClassName") then return "Instance" elseif builtin_type(v)=="table" and rawget(v, "__type") then return rawget(v, "__type") elseif builtin_type(v)=="table" and rawget(v, "X") and rawget(v, "Y") then return "Vector2" else return builtin_type(v) end end
iscclosure = function(f) return type(f)=="function" end
islclosure = iscclosure
newcclosure = function(f) assert(type(f)=="function", "function expected"); return f end
clonefunction = function(f) assert(type(f)=="function", "function expected"); return f end
getfenv = function(_) return _ENV end
setfenv = function(_, env) return env end
if not table.freeze then table.freeze=function(t) return t end end
utf8.nfcnormalize = utf8.nfcnormalize or function(s) return s end
utf8.nfdnormalize = utf8.nfdnormalize or function(s) return s end
)LUA";

static bool installPrelude(lua_State* L)
{
    Luau::CompileOptions options;
    const std::string bytecode = Luau::compile(kRobloxPrelude, options);
    if (luau_load(L, "=lumora.roblox", bytecode.data(), bytecode.size(), 0) != 0)
        return false;
    return lua_pcall(L, 0, 0, 0) == 0;
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

static int runScript(const char* path, int argc, char** argv, bool roblox, double timeout)
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
    lua_close(L);
    return rc;
}

static std::string jsonEscape(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value)
    {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += char(c);
    }
    return out + "\"";
}

int main(int argc, char** argv)
{
    bool roblox = true, json = false;
    double timeout = 0.0;
    std::vector<char*> scriptArgs;
    const char* script = nullptr;
    std::string parseError;
    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--no-roblox") roblox = false;
        else if (option == "--json") json = true;
        else if (option == "--help" || option == "-h")
        {
            std::cout << "usage: lumora [--no-roblox] [--json] [--timeout seconds] script.lua [args...]\n";
            return 0;
        }
        else if (option == "--version")
        {
            std::cout << "lumora 0.1.0\n";
            return 0;
        }
        else if (option == "--timeout" && i + 1 < argc)
        {
            try
            {
                timeout = std::stod(argv[++i]);
                if (!std::isfinite(timeout) || timeout < 0.0) throw std::invalid_argument("timeout must be a finite non-negative number");
            }
            catch (const std::exception& error) { parseError = error.what(); break; }
        }
        else if (!script && option.rfind("--", 0) != 0) script = argv[i];
        else if (script) scriptArgs.push_back(argv[i]);
        else { parseError = "unknown option: " + option; break; }
    }
    if (!parseError.empty())
    {
        std::cerr << parseError << "\n";
        return 2;
    }
    if (!script)
    {
        std::cerr << "usage: lumora [--no-roblox] [--json] [--timeout seconds] script.lua [args...]\n";
        return 2;
    }

    try
    {
#if defined(__unix__)
        if (json)
        {
            const std::string outPath = "/tmp/lumora-" + std::to_string(getpid()) + ".out";
            const std::string errPath = "/tmp/lumora-" + std::to_string(getpid()) + ".err";
            pid_t child = fork();
            if (child == 0)
            {
                FILE* out = fopen(outPath.c_str(), "w"); FILE* err = fopen(errPath.c_str(), "w");
                if (out) { dup2(fileno(out), STDOUT_FILENO); fclose(out); }
                if (err) { dup2(fileno(err), STDERR_FILENO); fclose(err); }
                std::vector<char*> args; args.push_back(argv[0]); args.push_back(const_cast<char*>(script));
                for (char* a : scriptArgs) args.push_back(a);
                int rc = runScript(script, int(args.size()), args.data(), roblox, timeout);
                std::cout.flush(); std::cerr.flush(); _exit(rc);
            }
            int status = 0; const auto started = std::chrono::steady_clock::now(); bool killed = false;
            while (waitpid(child, &status, WNOHANG) == 0)
            {
                if (timeout > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > timeout + 1.0)
                { kill(child, SIGKILL); killed = true; waitpid(child, &status, 0); break; }
                usleep(10000);
            }
            auto readText = [](const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); };
            const std::string stdoutText = readText(outPath), stderrText = readText(errPath);
            std::remove(outPath.c_str()); std::remove(errPath.c_str());
            int code = killed ? 124 : (WIFEXITED(status) ? WEXITSTATUS(status) : 1);
            std::cout << "{\"ok\":" << (code == 0 ? "true" : "false") << ",\"stdout\":" << jsonEscape(stdoutText)
                      << ",\"stderr\":" << jsonEscape(stderrText) << ",\"error\":" << jsonEscape(code == 0 ? "" : stderrText)
                      << ",\"exitCode\":" << code << "}\n";
            return code;
        }
#endif
        std::vector<char*> args; args.push_back(argv[0]); args.push_back(const_cast<char*>(script));
        for (char* a : scriptArgs) args.push_back(a);
        return runScript(script, int(args.size()), args.data(), roblox, timeout);
    }
    catch (const std::exception& error)
    {
        if (json) std::cout << "{\"ok\":false,\"stdout\":\"\",\"stderr\":\"\",\"error\":" << jsonEscape(error.what()) << ",\"exitCode\":2}\n";
        else std::cerr << error.what() << "\n";
        return 2;
    }
}
