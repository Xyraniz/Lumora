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
    setmetatable(t, { __metatable="The metatable is locked", __index = function(e, item) local v=setmetatable({ Name=item, EnumType=e, Value=#e._items, __type="EnumItem" }, { __metatable="The metatable is locked", __tostring=function() return "Enum."..enumName.."."..item end }); rawset(e, item, v); table.insert(e._items, v); return v end, __tostring=function() return "Enum."..enumName end })
    return t
end })

-- type and typeof are registered as native C functions by Lumora's
-- registerRobloxGlobals so that anti-tamper checks which verify builtins
-- are C closures pass correctly. The C implementation reads the __type
-- marker that the Roblox prelude sets on emulated userdata objects.

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
        -- Lemire's method: result = a + (width * pcgNext) >> 32
        -- This matches Roblox's Random:NextInteger exactly. Using mul32
        -- to get the high 32 bits of the 64-bit product avoids the
        -- floating-point precision loss that floor(rnd/2^32 * width) suffers.
        local rnd = pcgNext(self._state)
        local _, hi = mul32(width, rnd)
        return a + hi
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

-- typeof is registered as a native C function (see registerRobloxGlobals).
iscclosure = iscclosure
islclosure = islclosure
newcclosure = newcclosure
clonefunction = clonefunction
if not table.freeze then table.freeze=function(t) return t end end
utf8.nfcnormalize = utf8.nfcnormalize or function(s) return s end
utf8.nfdnormalize = utf8.nfdnormalize or function(s) return s end

-- ========== Executor globals ==========
-- getgenv: returns a shared table that persists across scripts (Roblox executor)
local _genv = _G
getgenv = function() return _genv end
getrenv = function() return _ENV end

-- setclipboard: no-op in headless mode
setclipboard = function(text) end

-- checkcaller: returns false (we are not the executor's internal caller)
checkcaller = function() return false end

-- cloneref / clonereference: return the same object (no ref cloning in Lumora)
cloneref = function(obj) return obj end
clonereference = function(obj) return obj end

-- hookmetamethod: return the original metamethod without hooking
hookmetamethod = function(mt, method, newFn)
    local old = getmetatable(mt)
    if old and old[method] then return old[method] end
    return function() end
end

-- hookfunction: return the original function without hooking
hookfunction = function(original, replacement) return original end

-- getnamecallmethod / setnamecallmethod
getnamecallmethod = function() return "" end
setnamecallmethod = function() end

-- getrawmetatable / setrawmetatable
getrawmetatable = function(obj) return getmetatable(obj) end
setrawmetatable = function(obj, mt) return setmetatable(obj, mt) end

-- gethui: return a folder-like instance for GUI parenting
gethui = function() return game:GetService("CoreGui") end

-- protectgui / syn.protect_gui: no-op
protectgui = function(gui) end
syn = syn or {}
syn.protect_gui = function(gui) end
syn.request = syn.request or function() return {StatusCode=200, Body="", Headers={}} end

-- request / http.request: stub HTTP
local function _stubRequest(opts)
    return { StatusCode = 200, Body = "", Headers = {}, Success = true }
end
request = _stubRequest
http = http or {}
http.request = _stubRequest
http.get = _stubRequest
http.post = _stubRequest
if not HttpService then
    HttpService = game:GetService("HttpService")
end

-- File system stubs
local _files = {}
writefile = function(path, content) _files[path] = content end
readfile = function(path) return _files[path] or "" end
isfile = function(path) return _files[path] ~= nil end
isfolder = function(path) return false end
makefolder = function(path) end
delfile = function(path) _files[path] = nil end
delfolder = function(path) end
listfiles = function(path) return {} end
appendfile = function(path, content) _files[path] = (_files[path] or "") .. content end
loadfile = function(path) return loadstring(readfile(path)) end

-- getconnections: return empty list
getconnections = function(signal) return {} end

-- isluau: true in Lumora
isluau = function() return true end

-- ========== Drawing API ==========
local drawing_mt = { __metatable = "Drawing" }
drawing_mt.__index = drawing_mt
function drawing_mt:Remove() self.Visible = false end
function drawing_mt:Destroy() self.Visible = false end

Drawing = {}
function Drawing.new(type)
    local obj = setmetatable({
        Type = type, Visible = false, Color = {R=255,G=255,B=255,A=255},
        Thickness = 1, Transparency = 1, Filled = false, Radius = 0,
        NumSides = 0, Position = Vector2.new(0,0), PointA = Vector2.new(0,0),
        PointB = Vector2.new(0,0), PointC = Vector2.new(0,0),
        From = Vector2.new(0,0), To = Vector2.new(0,0), Text = "",
        Size = Vector2.new(0,0), Center = false, Outline = false,
        OutlineColor = {R=0,G=0,B=0,A=255}, Font = 0, ZIndex = 0,
        __type = "Drawing"
    }, drawing_mt)
    return obj
end
Drawing.Fonts = { Plex = 0, Monospace = 1, System = 2, UI = 3 }

-- ========== Vector3 ==========
local function vec3(x,y,z)
    x, y, z = x or 0, y or 0, z or 0
    return setmetatable({X=x,Y=y,Z=z,__type="Vector3"}, {
        __index = { Magnitude = math.sqrt(x*x+y*y+z*z),
            Unit = setmetatable({X=x,Y=y,Z=z,__type="Vector3"}, {__tostring=function() return "Vector3" end}) },
        __add = function(a,b) return Vector3.new(a.X+b.X, a.Y+b.Y, a.Z+b.Z) end,
        __sub = function(a,b) return Vector3.new(a.X-b.X, a.Y-b.Y, a.Z-b.Z) end,
        __mul = function(a,b) if type(b)=="number" then return Vector3.new(a.X*b,a.Y*b,a.Z*b) end return Vector3.new(a.X*b.X,a.Y*b.Y,a.Z*b.Z) end,
        __div = function(a,b) if type(b)=="number" then return Vector3.new(a.X/b,a.Y/b,a.Z/b) end return Vector3.new(a.X/b.X,a.Y/b.Y,a.Z/b.Z) end,
        __tostring = function(v) return string.format("%g, %g, %g", v.X, v.Y, v.Z) end
    })
end
Vector3 = { new = vec3,
    fromAxis = function(n) return Vector3.new(0,1,0) end,
    zero = vec3(0,0,0), one = vec3(1,1,1),
    xAxis = vec3(1,0,0), yAxis = vec3(0,1,0), zAxis = vec3(0,0,1)
}

-- ========== CFrame ==========
local function cframe(x,y,z,r00,r01,r02,r10,r11,r12,r20,r21,r22)
    if r00 == nil then
        r00,r01,r02,r10,r11,r12,r20,r21,r22 = 1,0,0,0,1,0,0,0,1
    end
    return setmetatable({X=x or 0,Y=y or 0,Z=z or 0,
        R0={r00,r01,r02},R1={r10,r11,r12},R2={r20,r21,r22},
        Position=Vector3.new(x or 0,y or 0,z or 0), __type="CFrame"}, {
        __add = function(a,b) return CFrame.new(a.X+b.X, a.Y+b.Y, a.Z+b.Z) end,
        __sub = function(a,b) return CFrame.new(a.X-b.X, a.Y-b.Y, a.Z-b.Z) end,
        __mul = function(a,b)
            if type(b)=="number" then return a end
            if b.__type == "Vector3" then return Vector3.new(a.X, a.Y, a.Z) end
            return a
        end,
        __tostring = function(c) return string.format("%g, %g, %g", c.X, c.Y, c.Z) end
    })
end
CFrame = { new = cframe,
    identity = cframe(0,0,0),
    Angles = function(x,y,z) return cframe(0,0,0) end,
    fromEulerAnglesXYZ = function(x,y,z) return cframe(0,0,0) end,
    fromMatrix = function(pos,rx,ry,rz) return cframe(pos.X,pos.Y,pos.Z) end,
    lookAt = function(at,look) return cframe(at.X,at.Y,at.Z) end
}

-- ========== Color3 ==========
local function color3(r,g,b)
    r, g, b = r or 0, g or 0, b or 0
    return setmetatable({R=r,G=g,B=b,__type="Color3"}, {
        __tostring = function(c) return string.format("%g, %g, %g", c.R, c.G, c.B) end
    })
end
Color3 = { new = color3,
    fromRGB = function(r,g,b) return color3(r/255, g/255, b/255) end,
    fromHSV = function(h,s,v) return color3(0,0,0) end,
    fromHex = function(hex) return color3(0,0,0) end,
    toHSV = function(c) return 0, 0, 0 end,
    lerp = function(a,b,t) return color3(a.R+(b.R-a.R)*t, a.G+(b.G-a.G)*t, a.B+(b.B-a.B)*t) end
}

-- ========== UDim ==========
UDim = { new=function(scale,offset) return {Scale=scale,Offset=offset,__type="UDim"} end }

-- ========== BrickColor ==========
BrickColor = { new=function(...) return {Name="White",Number=1,Color=Color3.new(1,1,1),__type="BrickColor"} end,
    Red=function() return BrickColor.new() end, Blue=function() return BrickColor.new() end }

-- ========== Ray ==========
local function raynew(origin, direction)
    return setmetatable({Origin=origin,Direction=direction,__type="Ray"}, {
        __tostring = function() return "Ray" end
    })
end
Ray = { new = raynew }

-- ========== NumberRange ==========
NumberRange = { new=function(min,max) return {Min=min,Max=max,__type="NumberRange"} end }

-- ========== NumberSequence ==========
NumberSequence = { new=function(n) return {__type="NumberSequence"} end }

-- ========== ColorSequence ==========
ColorSequence = { new=function(c) return {__type="ColorSequence"} end }

-- ========== PhysicalProperties ==========
PhysicalProperties = { new=function(...) return {__type="PhysicalProperties"} end }

-- ========== TweenInfo ==========
TweenInfo = { new=function(time, easingDir, easingStyle, repeatCount, reverses, delay)
    return {Time=time or 1, EasingDirection=easingDir or "Out", EasingStyle=easingStyle or "Quad",
        RepeatCount=repeatCount or 0, Reverses=reverses or false, DelayTime=delay or 0, __type="TweenInfo"}
end }

-- ========== Extended Instance methods ==========
-- Add more methods to the existing Instance metatable
local _origIndex = instance_mt.__index
instance_mt.__index = function(self, key)
    local v = _origIndex(self, key)
    if v ~= nil then return v end

    if key == "FindFirstChildOfClass" then
        return function(obj, className)
            for _, c in ipairs(obj._children) do
                if c.ClassName == className then return c end
            end
            return nil
        end
    end
    if key == "FindFirstChildWhichIsA" then
        return function(obj, className, recursive)
            for _, c in ipairs(obj._children) do
                if c:IsA(className) then return c end
            end
            return nil
        end
    end
    if key == "FindFirstAncestorWhichIsA" then
        return function(obj, className)
            local p = obj.Parent
            while p do
                if p:IsA(className) then return p end
                p = p.Parent
            end
            return nil
        end
    end
    if key == "FindFirstAncestorOfClass" then
        return function(obj, className)
            local p = obj.Parent
            while p do
                if p.ClassName == className then return p end
                p = p.Parent
            end
            return nil
        end
    end
    if key == "FindFirstDescendant" then
        return function(obj, name)
            for _, c in ipairs(obj._children) do
                if c.Name == name then return c end
                local d = c:FindFirstDescendant(name)
                if d then return d end
            end
            return nil
        end
    end
    if key == "GetDescendants" then
        return function(obj)
            local result = {}
            local function walk(parent)
                for _, c in ipairs(parent._children) do
                    table.insert(result, c)
                    walk(c)
                end
            end
            walk(obj)
            return result
        end
    end
    if key == "IsDescendantOf" then
        return function(obj, ancestor)
            local p = obj.Parent
            while p do
                if p == ancestor then return true end
                p = p.Parent
            end
            return false
        end
    end
    if key == "IsAncestorOf" then
        return function(obj, descendant)
            return descendant:IsDescendantOf(obj)
        end
    end
    if key == "Clone" then
        return function(obj)
            local clone = Instance.new(obj.ClassName)
            clone.Name = obj.Name
            for k, v in pairs(rawget(obj, "_attributes") or {}) do
                clone:SetAttribute(k, v)
            end
            for _, c in ipairs(obj._children) do
                local cc = c:Clone()
                cc.Parent = clone
            end
            return clone
        end
    end
    if key == "GetPropertyChangedSignal" then
        return function(obj, prop)
            local sig = signal()
            obj._propSignals = obj._propSignals or {}
            obj._propSignals[prop] = sig
            return sig
        end
    end
    if key == "Raycast" then
        return function(obj, origin, direction, params)
            return nil
        end
    end
    if key == "GetDebugId" then
        return function(obj) return tostring(obj) end
    end
    if key == "GetChildren" then
        return function(obj) return obj._children end
    end
    if key == "GetAttributes" then
        return function(obj) return rawget(obj, "_attributes") or {} end
    end
    if key == "AddItem" then
        return function(obj, item) if item then item:Destroy() end end
    end
    if key == "ClearAllChildren" then
        return function(obj)
            while #obj._children > 0 do obj._children[1]:Destroy() end
        end
    end
    return rawget(self, key)
end

-- ========== game:HttpGet ==========
-- Load WindUI from the vendored file when the known URL is requested.
local _winduiCache = nil
local function _loadWindUI()
    if _winduiCache then return _winduiCache end
    -- readfile is a native C function that reads from the real filesystem.
    -- Try several candidate paths.
    local content = readfile("windui.lua")
    if not content or content == "" then
        content = readfile("/workspace/windui.lua")
    end
    if not content or content == "" then
        content = readfile("/workspace/lumora/build/bin/windui.lua")
    end
    if content and content ~= "" then
        _winduiCache = content
    else
        _winduiCache = "return function() return {} end"
    end
    return _winduiCache
end

-- game:HttpGet(url, nocache) — return cached content for known URLs
-- We wrap instance_mt.__index directly (instance_mt is a local in scope)
-- because getmetatable(game) returns the locked string, not the real table.
local _savedInstanceIndex = instance_mt.__index
instance_mt.__index = function(self, key)
    -- Only treat `game` (the root DataModel) specially here.
    if self == game then
        if key == "HttpGet" then
            return function(obj, url, nocache)
                if url and string.find(url, "WindUI", 1, true) then
                    return _loadWindUI()
                end
                -- Icons library URL — return a stub that provides the icon API
                if url and string.find(url, "Icons", 1, true) then
                    return [[
local module = {}
local icons = {}
local iconType = 'lucide'
function module.SetIconsType(t) iconType = t or 'lucide' end
function module.AddIcons(tbl) for k,v in pairs(tbl or {}) do icons[k] = v end end
function module.Icon(name, opts)
    return { Image = 'rbxasset://textures/ui/GuiImagePlaceholder.png',
             Name = name or '', ImageRectOffset = Vector2.new(0,0),
             ImageRectSize = Vector2.new(0,0), __type = 'Icon' }
end
function module.GetIcon(name) return icons[name] or module.Icon(name) end
function module.Icon2(name, opts) return module.Icon(name, opts) end
return module
]]
                end
                return ""
            end
        end
        if key == "HttpGetAsync" then
            return function(obj, url) return game:HttpGet(url) end
        end
        if key == "GetObjects" then
            return function(obj, url) return {} end
        end
        if key == "PostAsync" then
            return function(obj, url, data, contentType) return "" end
        end
        if key == "PlaceId" then return 2788229376 end
        if key == "JobId" then return "LUMORA_JOB_001" end
        if key == "GameId" then return 0 end
        if key == "CreatorId" then return 0 end
        if key == "CreatorType" then return Enum.CreatorType.User end
        if key == "Players" then return game:GetService("Players") end
        if key == "Workspace" then return workspace end
        if key == "Lighting" then return game:GetService("Lighting") end
        if key == "ReplicatedStorage" then return game:GetService("ReplicatedStorage") end
        if key == "RunService" then return game:GetService("RunService") end
        if key == "UserInputService" then return game:GetService("UserInputService") end
        if key == "TweenService" then return game:GetService("TweenService") end
        if key == "CoreGui" then return game:GetService("CoreGui") end
        if key == "HttpService" then return game:GetService("HttpService") end
        if key == "ContentProvider" then return game:GetService("ContentProvider") end
        if key == "MarketplaceService" then return game:GetService("MarketplaceService") end
        if key == "TeleportService" then return game:GetService("TeleportService") end
        if key == "SoundService" then return game:GetService("SoundService") end
        if key == "ContextActionService" then return game:GetService("ContextActionService") end
        if key == "VirtualInputManager" then return game:GetService("VirtualInputManager") end
        if key == "VirtualUser" then return game:GetService("VirtualUser") end
        if key == "ScriptContext" then return game:GetService("ScriptContext") end
        if key == "StarterGui" then return game:GetService("StarterGui") end
        if key == "CollectionService" then return game:GetService("CollectionService") end
    end
    return _savedInstanceIndex(self, key)
end

-- ========== Services with stub methods ==========
-- RunService
do
    local rs = game:GetService("RunService")
    rs._signals = { RenderStep = signal(), Heartbeat = signal(), Stepped = signal(), RenderStepped = signal() }
    function rs:BindToRenderStep(name, priority, fn) end
    function rs:UnbindFromRenderStep(name) end
    function rs:Heartbeat() return rs._signals.Heartbeat end
    function rs:RenderStepped() return rs._signals.RenderStepped end
    function rs:Stepped() return rs._signals.Stepped end
    rs.Heartbeat = rs._signals.Heartbeat
    rs.RenderStepped = rs._signals.RenderStepped
    rs.Stepped = rs._signals.Stepped
    function rs:IsStudio() return false end
    function rs:IsRunning() return true end
    function rs:IsClient() return true end
    function rs:IsServer() return false end
end

-- UserInputService
do
    local uis = game:GetService("UserInputService")
    uis._signals = { InputBegan = signal(), InputChanged = signal(), InputEnded = signal() }
    uis.InputBegan = uis._signals.InputBegan
    uis.InputChanged = uis._signals.InputChanged
    uis.InputEnded = uis._signals.InputEnded
    function uis:GetMouseLocation() return Vector2.new(0, 0) end
    function uis:GetMouseDelta() return Vector2.new(0, 0) end
    function uis:IsKeyDown(key) return false end
    function uis:GetMouseButtonsPressed() return {} end
    function uis:GetFocusedTextBox() return nil end
    function uis:TouchEnabled() return false end
    function uis:MouseEnabled() return true end
    function uis:KeyboardEnabled() return true end
    function uis:GamepadEnabled() return false end
    function uis:GetMouse() return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0) } end
end

-- Players
do
    local players = game:GetService("Players")
    players._signals = { PlayerAdded = signal(), PlayerRemoving = signal(), LocalPlayerAdded = signal() }
    players.PlayerAdded = players._signals.PlayerAdded
    players.PlayerRemoving = players._signals.PlayerRemoving
    function players:GetPlayers() return players._playerList or {} end
    function players:GetAllPlayers() return players:GetPlayers() end
    function players:GetPlayerFromCharacter(char)
        if not char then return nil end
        for _, p in ipairs(players:GetPlayers()) do
            if p.Character == char then return p end
        end
        return nil
    end
    function players:GetPlayerByUserId(id)
        for _, p in ipairs(players:GetPlayers()) do
            if p.UserId == id then return p end
        end
        return nil
    end
    -- Create a LocalPlayer
    local lp = Instance.new("Player")
    lp.Name = "LocalPlayer"
    lp.UserId = 12345678
    lp.Character = nil
    lp.Backpack = Instance.new("Backpack")
    lp.PlayerGui = Instance.new("PlayerGui")
    lp.PlayerScripts = Instance.new("PlayerScripts")
    lp.Backpack.Name = "Backpack"
    lp.PlayerGui.Name = "PlayerGui"
    lp.PlayerScripts.Name = "PlayerScripts"
    lp.Backpack.Parent = lp
    lp.PlayerGui.Parent = lp
    lp.PlayerScripts.Parent = lp
    players.LocalPlayer = lp
    players._playerList = { lp }
    lp.Parent = players

    -- Extend Player metatable
    local lp2 = lp
    function lp2:GetMouse()
        return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0),
                 Target = nil, Origin = Vector3.new(0,0,0), UnitRay = Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1)) }
    end
end

-- TweenService
do
    local ts = game:GetService("TweenService")
    function ts:Create(obj, tweenInfo, properties)
        local tween = setmetatable({ _obj = obj, _info = tweenInfo, _props = properties,
            PlaybackState = "Completed", __type = "Tween" }, {
            __index = {
                Play = function(self) end,
                Pause = function(self) end,
                Cancel = function(self) end,
                Destroy = function(self) end
            }
        })
        return tween
    end
end

-- HttpService
do
    local hs = game:GetService("HttpService")
    function hs:JSONEncode(tbl) return "{}" end
    function hs:JSONDecode(str) return {} end
    function hs:GenerateGUID(wrapInCurlyBraces)
        local guid = "00000000-0000-0000-0000-000000000000"
        if wrapInCurlyBraces == false then return guid end
        return "{" .. guid .. "}"
    end
    function hs:UrlEncode(str) return str or "" end
end

-- MarketplaceService
do
    local ms = game:GetService("MarketplaceService")
    function ms:GetProductInfo(assetId, infoType)
        return { AssetId = assetId, Name = "Asset", Description = "",
                 Creator = { Id = 0, Name = "Creator", Type = "User" },
                 ProductType = "Asset", Created = "2020-01-01T00:00:00Z" }
    end
    function ms:GetAssetInfo(assetId) return ms:GetProductInfo(assetId) end
end

-- ContentProvider
do
    local cp = game:GetService("ContentProvider")
    function cp:PreloadAsync(assets, callback)
        if callback then
            for _, a in ipairs(assets or {}) do callback({ Key = a, Succeeded = true, Status = "Success" }) end
        end
    end
end

-- Lighting
do
    local lighting = game:GetService("Lighting")
    lighting.ClockTime = 12
    lighting.Brightness = 2
    lighting.ExposureCompensation = 0
    lighting.FogEnd = 100000
    lighting.Ambient = Color3.new(0,0,0)
    lighting.GlobalShadows = true
    lighting.OutdoorAmbient = Color3.new(0.5, 0.5, 0.5)
end

-- ReplicatedStorage
game:GetService("ReplicatedStorage")

-- Camera setup
do
    local ws = workspace
    local cam = Instance.new("Camera")
    cam.Name = "Camera"
    cam.CFrame = CFrame.new(0, 10, 20)
    cam.Focus = CFrame.new(0, 0, 0)
    cam.FieldOfView = 70
    cam.NearPlaneSize = 0.1
    cam.FarPlane = 1000
    cam.ViewportSize = Vector2.new(1920, 1080)
    cam.CameraType = "Custom"
    ws.CurrentCamera = cam
    cam.Parent = ws

    function cam:WorldToViewportPoint(pos)
        return Vector3.new(960, 540, 50), true
    end
    function cam:ViewportPointToRay(x, y, depth)
        return Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1))
    end
    function cam:ScreenPointToRay(x, y, depth)
        return Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1))
    end
    function cam:GetPartsObscuringTarget(targets, ignoreList) return {} end
    function cam:Raycast(origin, direction, params) return nil end
end

-- VirtualInputManager / VirtualUser
do
    local vi = game:GetService("VirtualInputManager")
    function vi:SendKeyEvent(keyCode, key, isDown, sync) end
    function vi:SendMouseButtonEvent(x, y, button, isDown, sync) end
    function vi:SendMouseMoveEvent(x, y, sync) end
    function vi:SendMouseWheelEvent(x, y, scroll, sync) end

    local vu = game:GetService("VirtualUser")
    function vu:Button1Down(x, y, camera) end
    function vu:Button1Up(x, y, camera) end
    function vu:Button2Down(x, y, camera) end
    function vu:Button2Up(x, y, camera) end
    function vu:MoveCamera(cam, cframe, hitCFrame, sync) end
    function vu:CaptureFrame() end
end

-- ContextActionService
do
    local cas = game:GetService("ContextActionService")
    function cas:BindAction(actionName, fn, createTouchButton, ...) end
    function cas:UnbindAction(actionName) end
    function cas:BindActionAtPriority(actionName, fn, createTouchButton, priority, ...) end
    function cas:UnbindActionAtPriority(actionName, priority) end
end

-- ScriptContext
do
    local sc = game:GetService("ScriptContext")
    function sc:SetTimeout(duration, coreScriptsOnly) end
end

-- TeleportService
do
    local ts = game:GetService("TeleportService")
    function ts:Teleport(placeId, player, options, bindable) end
    function ts:TeleportToPlaceInstance(placeId, jobId, player, ...) end
    function ts:TeleportPartyAsync(placeId, players, options) return 0 end
end

-- SoundService
game:GetService("SoundService")

-- SetCoreGuiEnabled via StarterGui
do
    local sg = game:GetService("StarterGui")
    function sg:SetCoreGuiEnabled(gui, enabled) end
    function sg:GetCoreGuiEnabled(gui) return true end
    function sg:SetCore(name, value) end
    function sg:GetCore(name) return nil end
end

-- CollectionService
do
    local cs = game:GetService("CollectionService")
    function cs:GetTagged(tag) return {} end
    function cs:AddTag(instance, tag) end
    function cs:RemoveTag(instance, tag) end
    function cs:GetInstanceAddedSignal(tag) return signal() end
    function cs:GetInstanceRemovedSignal(tag) return signal() end
end

-- Animator / Humanoid stubs (set on Character instances)
local function _makeHumanoid()
    local h = Instance.new("Humanoid")
    h.Health = 100
    h.MaxHealth = 100
    h.WalkSpeed = 16
    h.JumpPower = 50
    h.RigType = Enum.HumanoidRigType.R15
    h.MoveDirection = Vector3.new(0,0,0)
    h._animator = Instance.new("Animator")
    h._animator.Name = "Animator"
    h._animator.Parent = h
    function h:GetPlayingAnimationTracks() return {} end
    function h:Move(direction, relative) end
    function h:Jump() end
    function h:ChangeState(state) end
    function h:GetState() return "Running" end
    function h:SetStateEnabled(state, enabled) end
    function h:LoadAnimation(anim) return { Play = function(self) end, Stop = function(self) end, AdjustSpeed = function(self, speed) end } end
    return h
end

-- Animator
do
    local anim = Instance.new("Animator")
    function anim:LoadAnimation(animTrack) return { Play = function() end, Stop = function() end } end
    function anim:GetPlayingAnimationTracks() return {} end
end

-- workspace extra properties
workspace.CurrentCamera = workspace:FindFirstChild("Camera")
if not workspace.CurrentCamera then
    workspace.CurrentCamera = Instance.new("Camera")
    workspace.CurrentCamera.Name = "Camera"
    workspace.CurrentCamera.Parent = workspace
end

-- SetCoreGuiEnabled global stub (some scripts call it on game)
function game:SetCoreGuiEnabled(gui, enabled) end
function game:GetCoreGuiEnabled(gui) return true end
)LUA";

// loadstring(source [, chunkName]) — mirrors the Roblox global. Compiles
// Luau source to bytecode and loads it as a function. Returns (fn) on
// success or (nil, errorMessage) on a compile error, matching the contract
// that Fengetheus AntiTamper and general Roblox scripts rely on.
static int lumora_loadstring(lua_State* L)
{
    const char* source = luaL_optstring(L, 1, "");
    const char* chunkName = luaL_optstring(L, 2, "loadstring");

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;

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

// readfile(path) — read a file from the real filesystem. This overrides
// the prelude's in-memory stub so that game:HttpGet can load vendored
// libraries (e.g. WindUI) from disk. Returns the file contents as a string,
// or an empty string if the file cannot be opened (matching Roblox semantics
// where readfile returns "" for missing files in most executors).
static int lumora_readfile(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, "");
    try
    {
        std::string content = readFile(path);
        lua_pushlstring(L, content.data(), content.size());
    }
    catch (const std::exception&)
    {
        lua_pushstring(L, "");
    }
    return 1;
}

// isfile(path) — check if a file exists on the real filesystem.
static int lumora_isfile(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, "");
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}

static bool installPrelude(lua_State* L)
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
// bytecode-level clone, and identity is sufficient for anti-tamper checks
// that verify clonefunction(pcall) still works.
static int lumora_clonefunction(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    return 1;
}

// Roblox-aware type(v) -> string. Identical to native Luau type() except that
// tables carrying a __type marker of "Instance", "Enums", "Enum", or
// "EnumItem" report "userdata", matching what real Roblox returns for its
// native objects. Implemented as a C closure so anti-tamper C-closure
// verification passes.
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
            if (strcmp(marker, "Instance") == 0 || strcmp(marker, "Enums") == 0 ||
                strcmp(marker, "Enum") == 0 || strcmp(marker, "EnumItem") == 0)
            {
                lua_pushstring(L, "userdata");
                return 1;
            }
        }
        lua_pop(L, 1);
    }
    lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
    return 1;
}

// Roblox-aware typeof(v) -> string. Returns the __type marker for emulated
// userdata, "Instance" for objects with a ClassName field, "Vector2" for
// {X=,Y=} tables, and falls back to native type otherwise.
static int lumora_typeof(lua_State* L)
{
    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TTABLE)
    {
        // Check ClassName first (Instance objects) — use rawget to avoid
        // triggering __index metamethods on emulated userdata.
        lua_pushstring(L, "ClassName");
        lua_rawget(L, 1);
        if (!lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_pushstring(L, "Instance");
            return 1;
        }
        lua_pop(L, 1);

        // Check __type marker
        lua_pushstring(L, "__type");
        lua_rawget(L, 1);
        if (lua_isstring(L, -1))
        {
            const char* marker = lua_tostring(L, -1);
            if (strcmp(marker, "Instance") == 0 || strcmp(marker, "Enums") == 0 ||
                strcmp(marker, "Enum") == 0 || strcmp(marker, "EnumItem") == 0 ||
                strcmp(marker, "Vector2") == 0 || strcmp(marker, "UDim2") == 0 ||
                strcmp(marker, "Path2D") == 0)
            {
                // Keep marker on stack, return it
                return 1;
            }
        }
        lua_pop(L, 1);

        // Check for Vector2-like {X=, Y=}
        lua_pushstring(L, "X");
        lua_rawget(L, 1);
        lua_pushstring(L, "Y");
        lua_rawget(L, 1);
        if (!lua_isnil(L, -2) && !lua_isnil(L, -1))
        {
            lua_pop(L, 2);
            lua_pushstring(L, "Vector2");
            return 1;
        }
        lua_pop(L, 2);
    }
    lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
    return 1;
}

static void registerRobloxGlobals(lua_State* L)
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

    // Roblox-aware type/typeof implemented as C closures so that anti-tamper
    // checks verifying builtins are C closures pass correctly.
    lua_pushcfunction(L, lumora_type, "type");
    lua_setglobal(L, "type");

    lua_pushcfunction(L, lumora_typeof, "typeof");
    lua_setglobal(L, "typeof");

    // Native readfile/isfile that read from the real filesystem, overriding
    // the prelude's in-memory stubs. This lets game:HttpGet load vendored
    // libraries like WindUI from disk.
    lua_pushcfunction(L, lumora_readfile, "readfile");
    lua_setglobal(L, "readfile");

    lua_pushcfunction(L, lumora_isfile, "isfile");
    lua_setglobal(L, "isfile");
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
    if (roblox)
        registerRobloxGlobals(L);
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
