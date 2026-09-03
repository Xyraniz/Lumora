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
    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--no-roblox") roblox = false;
        else if (option == "--json") json = true;
        else if (option == "--timeout" && i + 1 < argc) timeout = std::stod(argv[++i]);
        else if (!script) script = argv[i];
        else scriptArgs.push_back(argv[i]);
    }
    if (!script)
    {
        std::cerr << "usage: luau-vm [--no-roblox] [--json] [--timeout seconds] script.lua [args...]\n";
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
