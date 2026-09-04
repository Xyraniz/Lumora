#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <cstring>
#include <string>

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
    function s:Once(fn)
        local conn
        conn = s:Connect(function(...)
            if conn then conn:Disconnect() end
            fn(...)
        end)
        return conn
    end
    function s:Wait()
        -- In Roblox this yields until the signal fires. In Lumora we can't
        -- truly yield across the pcall boundary, so return nil immediately.
        -- This is sufficient for scripts that use :Wait() with fallback logic.
        return nil
    end
    function s:Fire(...)
        for _, x in ipairs(s._connections) do if x.c.Connected then x.fn(...) end end
    end
    function s:DisconnectAll()
        for _, x in ipairs(s._connections) do x.c.Connected = false end
        s._connections = {}
    end
    return s
end

local instance_mt = { __metatable = "The metatable is locked" }

-- Roblox class inheritance hierarchy. Each entry maps a class to its parent
-- class so IsA() can walk the chain (e.g. Part -> BasePart -> PVInstance ->
-- Instance). This mirrors the real Roblox class tree for the classes Lumora
-- emulates; classes not listed default to deriving from Instance.
local _classHierarchy = {
    Part = "BasePart",
    MeshPart = "BasePart",
    TrussPart = "BasePart",
    WedgePart = "BasePart",
    CornerWedgePart = "BasePart",
    BasePart = "PVInstance",
    PVInstance = "Instance",
    UnionOperation = "BasePart",
    NegateOperation = "BasePart",
    SpecialMesh = "Instance",
    Model = "Instance",
    Folder = "Instance",
    Camera = "Instance",
    Humanoid = "Instance",
    Animator = "Instance",
    Workspace = "World",
    World = "Instance",
    Player = "Instance",
    Backpack = "Instance",
    PlayerGui = "Instance",
    PlayerScripts = "Instance",
    ScreenGui = "GuiObject",
    Frame = "GuiObject",
    TextLabel = "GuiObject",
    TextButton = "GuiButton",
    ImageButton = "GuiButton",
    TextBox = "GuiButton",
    GuiButton = "GuiObject",
    GuiObject = "Instance",
    ScrollingFrame = "GuiObject",
    LocalScript = "Script",
    Script = "BaseScript",
    BaseScript = "Instance",
    ModuleScript = "Instance",
    Sound = "Instance",
    Animation = "Instance",
    AnimationController = "Instance",
    Motor6D = "JointInstance",
    Weld = "JointInstance",
    JointInstance = "Instance",
    BillboardGui = "Instance",
    SurfaceGui = "Instance",
    Fire = "Instance",
    Smoke = "Instance",
    Sparkles = "Instance",
    Attachment = "Instance",
    Decal = "Instance",
    Texture = "Decal",
    RemoteEvent = "Instance",
    RemoteFunction = "Instance",
    BindableEvent = "Instance",
    BindableFunction = "Instance",
    Folder = "Instance",
    DataModel = "Instance",
    Tween = "Instance",
    RunService = "Instance",
    UserInputService = "Instance",
    Players = "Instance",
    TweenService = "Instance",
    HttpService = "Instance",
    ReplicatedStorage = "Instance",
    Lighting = "Instance",
    ContentProvider = "Instance",
    MarketplaceService = "Instance",
    TeleportService = "Instance",
    SoundService = "Instance",
    ContextActionService = "Instance",
    VirtualInputManager = "Instance",
    VirtualUser = "Instance",
    ScriptContext = "Instance",
    StarterGui = "Instance",
    CollectionService = "Instance",
    Debris = "Instance",
    LocalizationService = "Instance",
    CoreGui = "Instance",
}

local function removeChild(parent, child)
    for i, value in ipairs(parent._children) do
        if value == child then table.remove(parent._children, i); return end
    end
end
instance_mt.__index = function(self, key)
    if key == "Parent" then return rawget(self, "_parent") end
    local properties = rawget(self, "_properties")
    if properties then
        local value = rawget(properties, key)
        if value ~= nil then return value end
    end
    if key == "GetChildren" then return function(obj) if obj == nil then error("Expected ':' not '.' calling member function GetChildren", 0) end return obj._children end end
    if key == "FindFirstChild" then return function(obj, name) for _, c in ipairs(obj._children) do if c.Name == name then return c end end return nil end end
    if key == "WaitForChild" then return function(obj, name, timeout)
        local child = obj:FindFirstChild(name)
        if child then return child end
        -- In headless mode we cannot truly yield. If a timeout was provided,
        -- return nil (matching Roblox behavior after timeout). Without a
        -- timeout, warn about infinite yield and return nil rather than
        -- hard-erroring, since the script may have fallback logic.
        if timeout then return nil end
        warn("Infinite yield possible on '" .. obj:GetFullName() .. ":WaitForChild(" .. name .. ")'")
        return nil
    end end
    if key == "GetService" then return function(obj, name) return obj._services[name] or Instance.new(name, obj) end end
    if key == "GetFullName" then return function(obj) local p=obj.Name; local q=obj.Parent; while q do p=q.Name.."."..p; q=q.Parent end return p end end
    if key == "Destroy" then return function(obj)
        if obj.Parent then obj.Parent = nil end
        while #obj._children > 0 do obj._children[1]:Destroy() end
        obj._destroyed = true
    end end
    if key == "IsA" then return function(obj, n)
        if n == "Instance" then return true end
        local cn = obj.ClassName
        if cn == n then return true end
        -- Walk the Roblox class hierarchy so IsA reports the inheritance
        -- chain correctly (e.g. Part is a BasePart, BasePart is an Instance).
        local chain = _classHierarchy[cn]
        while chain do
            if chain == n then return true end
            chain = _classHierarchy[chain]
        end
        return false
    end end
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
        local signals = rawget(self, "_propSignals")
        if signals and signals.Parent then signals.Parent:Fire(value) end
        return
    end
    local properties = rawget(self, "_properties")
    if properties and key:sub(1, 1) ~= "_" then
        local old = rawget(properties, key)
        rawset(properties, key, value)
        local signals = rawget(self, "_propSignals")
        if signals and old ~= value and signals[key] then signals[key]:Fire(value) end
        return
    end
    local old = rawget(self, key)
    rawset(self, key, value)
    local signals = rawget(self, "_propSignals")
    if signals and old ~= value and signals[key] then signals[key]:Fire(value) end
end

Instance = {}
function Instance.new(className, parent)
    local o = setmetatable({ __type="Instance", _properties={
        ClassName=className, Name=className, AttributeChanged=signal(),
        ChildAdded=signal(), ChildRemoved=signal()
    }, _children={}, _attributes={} }, instance_mt)
    -- BindableEvent: expose Event signal and Fire method like real Roblox
    if className == "BindableEvent" then
        o.Event = signal()
        o._fire = o.Event.Fire
        function o:Fire(...) self.Event:Fire(...) end
    end
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
-- registerRobloxGlobals so that builtins behave as C closures, matching
-- the contract that Roblox scripts expect. The C implementation reads
-- the __type marker that the Roblox prelude sets on emulated userdata
-- objects.

local function vec2(x,y)
    return setmetatable({X=x or 0,Y=y or 0,__type="Vector2"}, {
        __index = function(t, k)
            if k == "Magnitude" then return math.sqrt(t.X^2+t.Y^2) end
            if k == "Unit" then
                local m = math.sqrt(t.X^2+t.Y^2)
                if m == 0 then return vec2(0,0) end
                return vec2(t.X/m, t.Y/m)
            end
            if k == "Dot" then return function(a, b) return a.X*b.X + a.Y*b.Y end end
            if k == "Lerp" then return function(a, b, t)
                return vec2(a.X+(b.X-a.X)*t, a.Y+(b.Y-a.Y)*t)
            end end
            if k == "Cross" then return function(a, b) return a.X*b.Y - a.Y*b.X end end
            if k == "Angle" then return function(a, b, axis)
                local dot = a:Dot(b)
                local mag = a.Magnitude * b.Magnitude
                if mag == 0 then return 0 end
                return math.acos(math.max(-1, math.min(1, dot/mag)))
            end end
            return nil
        end,
        __add = function(a,b) return vec2(a.X+(b.X or 0), a.Y+(b.Y or 0)) end,
        __sub = function(a,b) return vec2(a.X-(b.X or 0), a.Y-(b.Y or 0)) end,
        __mul = function(a,b) if type(b)=="number" then return vec2(a.X*b,a.Y*b) end return vec2(a.X*b.X,a.Y*b.Y) end,
        __div = function(a,b) if type(b)=="number" then return vec2(a.X/b,a.Y/b) end return vec2(a.X/b.X,a.Y/b.Y) end,
        __unm = function(a) return vec2(-a.X, -a.Y) end,
        __eq = function(a,b) return a.X==b.X and a.Y==b.Y end,
        __tostring = function(v) return string.format("%g, %g",v.X,v.Y) end
    })
end
Vector2 = { new=vec2,
    zero = vec2(0,0), one = vec2(1,1)
}
UDim2 = {
    new=function(sx,ox,sy,oy)
        local ud = {X={Scale=sx,Offset=ox},Y={Scale=sy,Offset=oy},__type="UDim2"}
        setmetatable(ud, { __tostring = function(v)
            return string.format("{%g, %d, %g, %d}", v.X.Scale, v.X.Offset, v.Y.Scale, v.Y.Offset)
        end })
        return ud
    end,
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
    function r:NextUnitVector()
        -- Roblox returns a uniformly-distributed unit Vector3 on the unit
        -- sphere. We use the standard method: pick a random direction via
        -- two uniform samples and map to the sphere surface.
        local z = self:NextNumber(-1, 1)
        local theta = self:NextNumber(0, math.pi * 2)
        local r2 = math.sqrt(1 - z * z)
        return Vector3.new(r2 * math.cos(theta), r2 * math.sin(theta), z)
    end
    function r:Clone() local copy = Random.new(0); copy._state = {lo=self._state.lo, hi=self._state.hi}; return copy end
    r.__type = "Random"
    return r
end

local tasklib = {}
-- Scheduler: collects spawned/delayed coroutines. task.wait() yields,
-- and the scheduler resumes them after the main script body finishes.
-- This mirrors Roblox's cooperative scheduling model closely enough for
-- verification: the main script runs to completion (setting up UI, state,
-- connections), then spawned loops get a few resume cycles before we stop.
tasklib._threads = {}      -- active coroutines waiting to be resumed
tasklib._maxCycles = 50    -- safety: max scheduler iterations
tasklib._cycleCount = 0

function tasklib.spawn(fn, ...)
    local co = coroutine.create(fn)
    tasklib._threads[co] = table.pack(...) or { n = 0 }
    -- Resume immediately up to the first yield (e.g. task.wait())
    local args = tasklib._threads[co]
    local ok, err = coroutine.resume(co, table.unpack(args, 1, args.n))
    if not ok and coroutine.status(co) ~= "dead" then
        -- Propagate errors only if the coroutine died with an error
        if coroutine.status(co) == "dead" then error(err, 0) end
    end
    if coroutine.status(co) == "dead" then
        tasklib._threads[co] = nil
    end
    return co
end

function tasklib.delay(seconds, fn, ...)
    -- Keep delayed work pending until the scheduler. This preserves the
    -- cancellation window expected by Roblox-style code and contracts.
    local co = coroutine.create(fn)
    tasklib._threads[co] = table.pack(...)
    return co
end

function tasklib.cancel(co)
    if type(co) == "thread" then
        tasklib._threads[co] = nil
        pcall(coroutine.close, co)
    end
end

function tasklib.wait(seconds)
    -- Yield back to the caller; the scheduler will resume us later.
    coroutine.yield()
    return seconds or 0
end

function tasklib.defer(fn, ...) return tasklib.spawn(fn, ...) end

-- Run the scheduler: resume all active threads a limited number of times.
-- This is called after the main script body executes.
function tasklib._runScheduler()
    for cycle = 1, tasklib._maxCycles do
        tasklib._cycleCount = cycle
        local anyAlive = false
        for co, args in pairs(tasklib._threads) do
            if coroutine.status(co) ~= "dead" then
                anyAlive = true
                local ok, err = coroutine.resume(co, table.unpack(args, 1, args.n))
                if not ok then
                    -- Silently drop errored threads (Roblox warns but continues)
                    tasklib._threads[co] = nil
                end
                if coroutine.status(co) == "dead" then
                    tasklib._threads[co] = nil
                end
            else
                tasklib._threads[co] = nil
            end
        end
        if not anyAlive then break end
    end
end

 task = tasklib

-- tick(): returns current time in seconds (Roblox global)
local _tickStart = os.clock()
tick = function() return os.clock() - _tickStart end
-- time(): alias for tick in some contexts
time = tick

-- wait/delay/spawn: Roblox-style globals (in addition to task.* variants)
wait = function(seconds) return tasklib.wait(seconds) end
delay = function(seconds, fn) return tasklib.delay(seconds, fn) end
spawn = function(fn, ...) return tasklib.spawn(fn, ...) end

-- warn(): like print but prefixed with warning
warn = function(...) print("[Warning]", ...) end

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

-- File system compatibility layer.
-- This is deliberately process-local and in-memory: it never touches the host
-- filesystem. It is useful for scripts that persist small fixtures or modules
-- while keeping Lumora suitable for reproducible tests.
local _files = {}
local _folders = { [""] = true }
local function _normalizePath(path)
    assert(type(path) == "string", "path must be a string")
    path = path:gsub("\\\\", "/"):gsub("^/+", ""):gsub("/+", "/")
    path = path:gsub("^%./", "")
    assert(path ~= "" and not path:find("%.%./", 1, true) and path ~= "..", "invalid path")
    return path
end
local function _parentPath(path)
    return path:match("^(.*)/[^/]+$") or ""
end
local function _ensureFolders(path)
    local current = ""
    for part in path:gmatch("[^/]+") do
        current = current == "" and part or current .. "/" .. part
        _folders[current] = true
    end
end
local function _validateParent(path)
    local parent = _parentPath(path)
    if parent ~= "" and not _folders[parent] then
        _ensureFolders(parent)
    end
end
writefile = function(path, content)
    path = _normalizePath(path); _validateParent(path)
    _files[path] = tostring(content or "")
end
readfile = function(path)
    path = _normalizePath(path)
    assert(_files[path] ~= nil, "file does not exist: " .. path)
    return _files[path]
end
isfile = function(path)
    local ok, normalized = pcall(_normalizePath, path)
    return ok and _files[normalized] ~= nil
end
isfolder = function(path)
    local ok, normalized = pcall(_normalizePath, path)
    return ok and _folders[normalized] == true
end
makefolder = function(path)
    path = _normalizePath(path); _ensureFolders(path)
end
delfile = function(path)
    path = _normalizePath(path); _files[path] = nil
end
delfolder = function(path)
    path = _normalizePath(path)
    for file in pairs(_files) do
        if file == path or file:sub(1, #path + 1) == path .. "/" then _files[file] = nil end
    end
    for folder in pairs(_folders) do
        if folder == path or folder:sub(1, #path + 1) == path .. "/" then _folders[folder] = nil end
    end
end
listfiles = function(path)
    local normalized = ""
    if path and path ~= "" then normalized = _normalizePath(path) end
    local prefix = normalized == "" and "" or normalized .. "/"
    local result, seen = {}, {}
    local function add(value)
        if not seen[value] then seen[value] = true; table.insert(result, value) end
    end
    for file in pairs(_files) do
        if file:sub(1, #prefix) == prefix then
            local rest = file:sub(#prefix + 1)
            if rest ~= "" and not rest:find("/", 1, true) then add(file) end
        end
    end
    for folder in pairs(_folders) do
        if folder ~= "" and folder:sub(1, #prefix) == prefix then
            local rest = folder:sub(#prefix + 1)
            if rest ~= "" and not rest:find("/", 1, true) then add(folder) end
        end
    end
    table.sort(result)
    return result
end
appendfile = function(path, content)
    path = _normalizePath(path); _validateParent(path)
    _files[path] = (_files[path] or "") .. tostring(content or "")
end
loadfile = function(path)
    path = _normalizePath(path)
    return loadstring(readfile(path), "@" .. path)
end

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
        __index = function(t, k)
            if k == "Magnitude" then return math.sqrt(t.X^2+t.Y^2+t.Z^2) end
            if k == "Unit" then
                local m = math.sqrt(t.X^2+t.Y^2+t.Z^2)
                if m == 0 then return vec3(0,0,0) end
                return vec3(t.X/m, t.Y/m, t.Z/m)
            end
            if k == "Dot" then return function(a, b) return a.X*b.X + a.Y*b.Y + a.Z*b.Z end end
            if k == "Cross" then return function(a, b)
                return vec3(a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X)
            end end
            if k == "Lerp" then return function(a, b, t)
                return vec3(a.X+(b.X-a.X)*t, a.Y+(b.Y-a.Y)*t, a.Z+(b.Z-a.Z)*t)
            end end
            if k == "Angle" then return function(a, b, axis)
                local dot = a:Dot(b)
                local mag = a.Magnitude * b.Magnitude
                if mag == 0 then return 0 end
                return math.acos(math.max(-1, math.min(1, dot/mag)))
            end end
            return nil
        end,
        __add = function(a,b) return vec3(a.X+(b.X or 0), a.Y+(b.Y or 0), a.Z+(b.Z or 0)) end,
        __sub = function(a,b) return vec3(a.X-(b.X or 0), a.Y-(b.Y or 0), a.Z-(b.Z or 0)) end,
        __mul = function(a,b) if type(b)=="number" then return vec3(a.X*b,a.Y*b,a.Z*b) end return vec3(a.X*b.X,a.Y*b.Y,a.Z*b.Z) end,
        __div = function(a,b) if type(b)=="number" then return vec3(a.X/b,a.Y/b,a.Z/b) end return vec3(a.X/b.X,a.Y/b.Y,a.Z/b.Z) end,
        __unm = function(a) return vec3(-a.X, -a.Y, -a.Z) end,
        __eq = function(a,b) return a.X==b.X and a.Y==b.Y and a.Z==b.Z end,
        __tostring = function(v) return string.format("%g, %g, %g", v.X, v.Y, v.Z) end
    })
end
Vector3 = { new = vec3,
    fromAxis = function(n) return Vector3.new(0,1,0) end,
    zero = vec3(0,0,0), one = vec3(1,1,1),
    xAxis = vec3(1,0,0), yAxis = vec3(0,1,0), zAxis = vec3(0,0,1)
}

-- ========== CFrame ==========
-- A CFrame stores a position and a 3x3 rotation matrix. We implement real
-- arithmetic so CFrame*CFrame composes transforms, CFrame*Vector3 transforms
-- points, Angles builds a rotation matrix, and lookAt points the -Z axis
-- toward the target -- matching Roblox's observable behavior.
local function cframe(x,y,z,r00,r01,r02,r10,r11,r12,r20,r21,r22)
    if r00 == nil then
        r00,r01,r02,r10,r11,r12,r20,r21,r22 = 1,0,0,0,1,0,0,0,1
    end
    local pos = Vector3.new(x or 0, y or 0, z or 0)
    return setmetatable({X=x or 0,Y=y or 0,Z=z or 0,
        R0={r00,r01,r02},R1={r10,r11,r12},R2={r20,r21,r22},
        Position=pos, __type="CFrame"}, {
        __add = function(a,b) return cframe(a.X+b.X, a.Y+b.Y, a.Z+b.Z) end,
        __sub = function(a,b) return cframe(a.X-b.X, a.Y-b.Y, a.Z-b.Z) end,
        __mul = function(a,b)
            if b.__type == "Vector3" then
                local nx = a.X + a.R0[1]*b.X + a.R0[2]*b.Y + a.R0[3]*b.Z
                local ny = a.Y + a.R1[1]*b.X + a.R1[2]*b.Y + a.R1[3]*b.Z
                local nz = a.Z + a.R2[1]*b.X + a.R2[2]*b.Y + a.R2[3]*b.Z
                return Vector3.new(nx, ny, nz)
            end
            if type(b) == "number" then return cframe(a.X*b, a.Y*b, a.Z*b) end
            if b.__type == "CFrame" then
                local function matmul(m1, m2)
                    local out = {}
                    for row = 1, 3 do
                        out[row] = {}
                        for col = 1, 3 do
                            out[row][col] = m1[row][1]*m2[1][col] + m1[row][2]*m2[2][col] + m1[row][3]*m2[3][col]
                        end
                    end
                    return out
                end
                local rot = matmul({a.R0, a.R1, a.R2}, {b.R0, b.R1, b.R2})
                local px = a.X + a.R0[1]*b.X + a.R0[2]*b.Y + a.R0[3]*b.Z
                local py = a.Y + a.R1[1]*b.X + a.R1[2]*b.Y + a.R1[3]*b.Z
                local pz = a.Z + a.R2[1]*b.X + a.R2[2]*b.Y + a.R2[3]*b.Z
                return cframe(px, py, pz,
                    rot[1][1], rot[1][2], rot[1][3],
                    rot[2][1], rot[2][2], rot[2][3],
                    rot[3][1], rot[3][2], rot[3][3])
            end
            return a
        end,
        __eq = function(a,b) return a.X==b.X and a.Y==b.Y and a.Z==b.Z end,
        __tostring = function(c) return string.format("%g, %g, %g", c.X, c.Y, c.Z) end,
        __index = function(self, key)
            if key == "Position" then return Vector3.new(self.X, self.Y, self.Z) end
            if key == "LookVector" then return Vector3.new(-self.R2[1], -self.R2[2], -self.R2[3]) end
            if key == "RightVector" then return Vector3.new(self.R0[1], self.R0[2], self.R0[3]) end
            if key == "UpVector" then return Vector3.new(self.R1[1], self.R1[2], self.R1[3]) end
            if key == "XVector" then return Vector3.new(self.R0[1], self.R0[2], self.R0[3]) end
            if key == "YVector" then return Vector3.new(self.R1[1], self.R1[2], self.R1[3]) end
            if key == "ZVector" then return Vector3.new(self.R2[1], self.R2[2], self.R2[3]) end
            if key == "Inverse" then
                return function(cf)
                    local t = {cf.R0, cf.R1, cf.R2}
                    local inv = {
                        {t[1][1], t[2][1], t[3][1]},
                        {t[1][2], t[2][2], t[3][2]},
                        {t[1][3], t[2][3], t[3][3]},
                    }
                    local px = -(inv[1][1]*cf.X + inv[1][2]*cf.Y + inv[1][3]*cf.Z)
                    local py = -(inv[2][1]*cf.X + inv[2][2]*cf.Y + inv[2][3]*cf.Z)
                    local pz = -(inv[3][1]*cf.X + inv[3][2]*cf.Y + inv[3][3]*cf.Z)
                    return cframe(px, py, pz,
                        inv[1][1], inv[1][2], inv[1][3],
                        inv[2][1], inv[2][2], inv[2][3],
                        inv[3][1], inv[3][2], inv[3][3])
                end
            end
            if key == "PointToObjectSpace" then
                return function(cf, v)
                    local dx, dy, dz = v.X - cf.X, v.Y - cf.Y, v.Z - cf.Z
                    return Vector3.new(cf.R0[1]*dx + cf.R0[2]*dy + cf.R0[3]*dz,
                                       cf.R1[1]*dx + cf.R1[2]*dy + cf.R1[3]*dz,
                                       cf.R2[1]*dx + cf.R2[2]*dy + cf.R2[3]*dz)
                end
            end
            if key == "PointToWorldSpace" then return function(cf, v) return cf * v end end
            if key == "VectorToObjectSpace" then
                return function(cf, v)
                    return Vector3.new(cf.R0[1]*v.X + cf.R0[2]*v.Y + cf.R0[3]*v.Z,
                                       cf.R1[1]*v.X + cf.R1[2]*v.Y + cf.R1[3]*v.Z,
                                       cf.R2[1]*v.X + cf.R2[2]*v.Y + cf.R2[3]*v.Z)
                end
            end
            if key == "VectorToWorldSpace" then
                return function(cf, v)
                    return Vector3.new(cf.R0[1]*v.X + cf.R0[2]*v.Y + cf.R0[3]*v.Z,
                                       cf.R1[1]*v.X + cf.R1[2]*v.Y + cf.R1[3]*v.Z,
                                       cf.R2[1]*v.X + cf.R2[2]*v.Y + cf.R2[3]*v.Z)
                end
            end
            if key == "ToEulerAnglesXYZ" then return function(cf) return 0, 0, 0 end end
            if key == "ToOrientation" then return function(cf) return 0, 0, 0 end end
            return nil
        end,
    })
end
CFrame = { new = cframe,
    identity = cframe(0,0,0),
    Angles = function(x,y,z)
        local cx, sx = math.cos(x), math.sin(x)
        local cy, sy = math.cos(y), math.sin(y)
        local cz, sz = math.cos(z), math.sin(z)
        return cframe(0,0,0,
            cy*cz, -cy*sz, sy,
            sx*sy*cz + cx*sz, -sx*sy*sz + cx*cz, -sx*cy,
            -cx*sy*cz + sx*sz, cx*sy*sz + sx*cz, cx*cy)
    end,
    fromEulerAnglesXYZ = function(x,y,z) return CFrame.Angles(x,y,z) end,
    fromOrientation = function(x,y,z) return CFrame.Angles(x,y,z) end,
    fromMatrix = function(pos, rx, ry, rz)
        if not rz then rz = rx:Cross(ry) end
        return cframe(pos.X, pos.Y, pos.Z,
            rx.X, ry.X, rz.X,
            rx.Y, ry.Y, rz.Y,
            rx.Z, ry.Z, rz.Z)
    end,
    lookAt = function(at, look, up)
        up = up or Vector3.new(0,1,0)
        local forward = Vector3.new(look.X-at.X, look.Y-at.Y, look.Z-at.Z)
        local mag = forward.Magnitude
        if mag < 1e-6 then return cframe(at.X, at.Y, at.Z) end
        forward = Vector3.new(forward.X/mag, forward.Y/mag, forward.Z/mag)
        local right = up:Cross(forward)
        local rmag = right.Magnitude
        if rmag < 1e-6 then
            up = Vector3.new(0,0,1)
            right = up:Cross(forward)
            rmag = right.Magnitude
        end
        right = Vector3.new(right.X/rmag, right.Y/rmag, right.Z/rmag)
        local realUp = forward:Cross(right)
        return cframe(at.X, at.Y, at.Z,
            right.X, realUp.X, -forward.X,
            right.Y, realUp.Y, -forward.Y,
            right.Z, realUp.Z, -forward.Z)
    end
}

-- ========== Color3 ==========
-- Roblox's Color3 stores components in the 0..1 range. We expose both the
-- PascalCase instance methods (ToHex, ToHSV, Lerp) and the legacy lowercase
-- static helpers (Color3.toHex, Color3.toHSV) that some executor scripts
-- still call, so both calling conventions work.
local function _color3ToHex(c)
    local r = math.floor((c.R or 0) * 255 + 0.5)
    local g = math.floor((c.G or 0) * 255 + 0.5)
    local b = math.floor((c.B or 0) * 255 + 0.5)
    return string.format("%02x%02x%02x", r, g, b)
end

-- Convert a 0..1 RGB triple to HSV (h,s,v in 0..1). Matches Roblox's
-- Color3:ToHSV() which returns (hue, saturation, value).
local function _rgbToHsv(r, g, b)
    local max = math.max(r, g, b)
    local min = math.min(r, g, b)
    local delta = max - min
    local v = max
    local s = max == 0 and 0 or (delta / max)
    local h = 0
    if delta > 0 then
        if max == r then
            h = ((g - b) / delta) % 6
        elseif max == g then
            h = (b - r) / delta + 2
        else
            h = (r - g) / delta + 4
        end
        h = h / 6
        if h < 0 then h = h + 1 end
    end
    return h, s, v
end

local function color3(r,g,b)
    r, g, b = r or 0, g or 0, b or 0
    return setmetatable({R=r,G=g,B=b,__type="Color3"}, {
        __index = function(self, key)
            -- Instance methods (PascalCase, the canonical Roblox API)
            if key == "ToHex" then return function(c) return _color3ToHex(c) end end
            if key == "ToHSV" then return function(c) return _rgbToHsv(c.R, c.G, c.B) end end
            if key == "Lerp" then return function(c, other, t)
                return color3(c.R+(other.R-c.R)*t, c.G+(other.G-c.G)*t, c.B+(other.B-c.B)*t)
            end end
            -- Legacy lowercase aliases for executor compatibility
            if key == "toHex" then return function(c) return _color3ToHex(c) end end
            if key == "toHSV" then return function(c) return _rgbToHsv(c.R, c.G, c.B) end end
            return nil
        end,
        __tostring = function(c) return string.format("%g, %g, %g", c.R, c.G, c.B) end,
        __eq = function(a,b) return a.R==b.R and a.G==b.G and a.B==b.B end,
    })
end
Color3 = { new = color3,
    fromRGB = function(r,g,b) return color3(r/255, g/255, b/255) end,
    fromHSV = function(h,s,v)
        -- Simple HSV to RGB conversion
        local i = math.floor(h * 6)
        local f = h * 6 - i
        local p = v * (1 - s)
        local q = v * (1 - f * s)
        local t = v * (1 - (1 - f) * s)
        i = i % 6
        if i == 0 then return color3(v, t, p)
        elseif i == 1 then return color3(q, v, p)
        elseif i == 2 then return color3(p, v, t)
        elseif i == 3 then return color3(p, q, v)
        elseif i == 4 then return color3(t, p, v)
        else return color3(v, p, q) end
    end,
    fromHex = function(hex)
        if not hex or type(hex) ~= "string" then return color3(0,0,0) end
        hex = hex:gsub("^#","")
        if #hex == 3 then hex = hex:sub(1,1)..hex:sub(1,1)..hex:sub(2,2)..hex:sub(2,2)..hex:sub(3,3)..hex:sub(3,3) end
        if #hex ~= 6 then return color3(0,0,0) end
        local r = tonumber(hex:sub(1,2), 16) or 0
        local g = tonumber(hex:sub(3,4), 16) or 0
        local b = tonumber(hex:sub(5,6), 16) or 0
        return color3(r/255, g/255, b/255)
    end,
    toHSV = function(c) return _rgbToHsv(c.R, c.G, c.B) end,
    toHex = function(c) return _color3ToHex(c) end,
    lerp = function(a,b,t) return color3(a.R+(b.R-a.R)*t, a.G+(b.G-a.G)*t, a.B+(b.B-a.B)*t) end
}

-- ========== UDim ==========
UDim = { new=function(scale, offset)
    return {Scale=scale or 0, Offset=offset or 0, __type="UDim"}
end }

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

-- ========== RaycastParams ==========
RaycastParams = { new=function()
    return { FilterType = "Exclude", FilterDescendantsInstances = {},
        IgnoreWater = false, RespectCanCollide = false, __type = "RaycastParams" }
end }

-- ========== NumberRange ==========
NumberRange = { new=function(min,max) return {Min=min,Max=max,__type="NumberRange"} end }

-- ========== NumberSequence ==========
NumberSequence = { new=function(n) return {__type="NumberSequence"} end }

-- ========== NumberSequenceKeypoint ==========
NumberSequenceKeypoint = { new=function(time, value)
    return {Time=time or 0, Value=value or 0, Envelope=0, __type="NumberSequenceKeypoint"}
end }

-- ========== ColorSequence ==========
ColorSequence = { new=function(c) return {__type="ColorSequence"} end }

-- ========== ColorSequenceKeypoint ==========
ColorSequenceKeypoint = { new=function(time, color)
    return {Time=time or 0, Value=color or Color3.new(1,1,1), __type="ColorSequenceKeypoint"}
end }

-- ========== PhysicalProperties ==========
PhysicalProperties = { new=function(...) return {__type="PhysicalProperties"} end }

-- ========== Font ==========
Font = { new=function(face, weight, style)
    return {Family="rbxasset://fonts/families/SourceSansPro.json", Weight=weight or "Regular",
        Style=style or "Normal", __type="Font"}
end }
Font.fromEnum = function(enum) return Font.new() end
Font.fromId = function(id) return Font.new() end
Font.fromName = function(name, weight, style) return Font.new(nil, weight, style) end

-- ========== Rect ==========
Rect = { new=function(minX, minY, maxX, maxY)
    return {Min=Vector2.new(minX or 0, minY or 0), Max=Vector2.new(maxX or 0, maxY or 0), Width=(maxX or 0)-(minX or 0), Height=(maxY or 0)-(minY or 0), __type="Rect"}
end }

-- ========== UDim ==========
UDim = { new=function(scale, offset)
    return {Scale=scale or 0, Offset=offset or 0, __type="UDim"}
end }

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
            assert(type(prop) == "string", "property name must be a string")
            obj._propSignals = obj._propSignals or {}
            if not obj._propSignals[prop] then obj._propSignals[prop] = signal() end
            return obj._propSignals[prop]
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
    -- Common Sound methods (available on all instances, harmless no-ops for non-Sounds)
    if key == "Play" then return function(obj) end end
    if key == "Stop" then return function(obj) end end
    if key == "Pause" then return function(obj) end end
    if key == "Resume" then return function(obj) end end
    -- Common Instance properties that should return defaults
    if key == "IsLoaded" then return function(obj) return true end end
    if key == "WaitForProperty" then return function(obj, prop) return obj[prop] end end
    -- ScrollingFrame / GuiObject computed properties
    if key == "AbsoluteCanvasSize" then return Vector2.new(0, 0) end
    if key == "AbsoluteWindowSize" then return Vector2.new(0, 0) end
    if key == "AbsolutePosition" then return Vector2.new(0, 0) end
    if key == "AbsoluteSize" then return Vector2.new(0, 0) end
    -- TextLabel/TextButton computed properties
    if key == "TextBounds" then return Vector2.new(0, 0) end
    if key == "TextContent" then return rawget(self, "Text") or "" end
    -- Common default properties
    if key == "Transparency" then return rawget(self, "Transparency") or 0 end
    if key == "Visible" then return rawget(self, "Visible") end
    if key == "AnchorPoint" then return rawget(self, "AnchorPoint") or Vector2.new(0, 0) end
    if key == "Position" then return rawget(self, "Position") or UDim2.new(0, 0, 0, 0) end
    if key == "Size" then return rawget(self, "Size") or UDim2.new(0, 0, 0, 0) end
    if key == "BackgroundColor3" then return rawget(self, "BackgroundColor3") or Color3.new(1, 1, 1) end
    if key == "TextColor3" then return rawget(self, "TextColor3") or Color3.new(1, 1, 1) end
    if key == "TextSize" then return rawget(self, "TextSize") or 14 end
    if key == "Text" then return rawget(self, "Text") or "" end
    if key == "FontFace" then return rawget(self, "FontFace") or Font.new() end
    -- Fallback: return sensible defaults for any other property name pattern.
    -- This prevents "attempt to index nil" errors when WindUI accesses hundreds
    -- of Roblox Instance properties that don't exist in our emulation.
    local raw = rawget(self, key)
    if raw ~= nil then return raw end
    -- Vector2 properties
    if key == "CanvasPosition" or key == "ScrollPosition" or key == "GUIInset" or
       key == "ScreenSize" or key == "ViewSize" then return UDim2.new(0, 0, 0, 0) end
    -- Boolean properties
    if key == "Visible" or key == "Active" or key == "Draggable" or
       key == "ClipsDescendants" or key == "Archivable" or
       key == "AutoButtonColor" or key == "Modal" or key == "ScrollingEnabled" or
       key == "AutomaticCanvasSize" or key == "CanvasSize" or
       key == "ScrollBarThickness" or key == "BorderSizePixel" or
       key == "ZIndex" or key == "LayoutOrder" or key == "Rotation" or
       key == "SelectionOrder" or key == "MaxTextWidth" or
       key == "RichText" or key == "TextTruncate" or key == "TextWrapped" or
       key == "TextXAlignment" or key == "TextYAlignment" or
       key == "TextScaled" or key == "MaxVisibleGraphemes" or
       key == "BorderMode" or key == "Shape" or key == "CornerRadius" then
        return rawget(self, key) or 0
    end
    -- String properties
    if key == "Name" then return self.Name or "" end
    if key == "ClassName" then return self.ClassName or "" end
    -- If still not found, return nil (let the caller handle it)
    return nil
end

-- ========== game:HttpGet ==========
-- Load WindUI from the vendored file when the known URL is requested.
-- We provide a comprehensive stub that implements all the WindUI methods
-- the Da Hood script uses, instead of loading the full 20k-line WindUI
-- library (which requires a complete Roblox GUI rendering system).
local _winduiCache = nil

-- Create a WindUI stub element (tabs, toggles, sliders, etc.)
local function _makeElement()
    local el = {
        __type = "WindUIElement",
        _flags = {},
        Enabled = false,
        Value = false,
    }
    -- Elements that act as containers (tabs) have sub-creator methods
    function el:Toggle(opts)
        opts = opts or {}
        if opts.Value ~= nil then self._flags[opts.Flag] = opts.Value end
        local sub = _makeElement()
        sub.Value = opts.Value or false
        sub.Enabled = opts.Value or false
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback and opts.Value then pcall(opts.Callback, opts.Value) end
        return sub
    end
    function el:Slider(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or opts.Min or 0
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Dropdown(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or opts.Default or ""
        sub.Options = opts.Options or {}
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Colorpicker(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or Color3.fromRGB(255, 255, 255)
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Button(opts)
        opts = opts or {}
        if opts.Callback then pcall(opts.Callback) end
        return _makeElement()
    end
    function el:Input(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or ""
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Paragraph(opts)
        opts = opts or {}
        return _makeElement()
    end
    function el:Divider() return _makeElement() end
    function el:Select(opts) return el:Dropdown(opts) end
    function el:Keybind(opts) return el:Toggle(opts) end
    return el
end

local function _makeTab()
    local tab = _makeElement()
    tab._flags = {}
    return tab
end

local function _makeWindow()
    local w = _makeElement()
    w._flags = {}
    w._tabs = {}
    function w:Tab(opts)
        opts = opts or {}
        local t = _makeTab()
        t.Title = opts.Title or ""
        t.Icon = opts.Icon
        table.insert(w._tabs, t)
        return t
    end
    function w:GetFlag(name) return w._flags[name] end
    function w:GetFlagElement(name) return w._flags[name] end
    function w:ListFlags() return w._flags end
    function w:Open() end
    function w:Close() end
    function w:OnDestroy(fn) end
    function w:SetBackgroundImage(img) end
    function w:SetBackgroundImageTransparency(val) end
    function w:SetIconSize(size) end
    function w:Destroy() end
    return w
end

local function _makeWindUI()
    local ui = {}
    ui._themes = {
        Dark = { Name="Dark", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(255,255,255),
            Background=Color3.fromRGB(30,30,30), BorderColor=Color3.fromRGB(50,50,50) },
        Light = { Name="Light", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(0,0,0),
            Background=Color3.fromRGB(255,255,255), BorderColor=Color3.fromRGB(200,200,200) },
        Graphite = { Name="Graphite", Accent=Color3.fromRGB(80,80,80), TextColor=Color3.fromRGB(255,255,255),
            Background=Color3.fromRGB(20,20,20), BorderColor=Color3.fromRGB(40,40,40) },
    }
    function ui:CreateWindow(opts)
        return _makeWindow()
    end
    function ui:GetThemes() return ui._themes end
    function ui:SetTheme(name) end
    function ui:AddTheme(theme)
        if theme and theme.Name then ui._themes[theme.Name] = theme end
    end
    function ui:SetLanguage(lang) end
    function ui:Localization(opts) end
    function ui:Notify(opts) end
    function ui:Save() end
    function ui:Load() end
    function ui:SetIconSize(size) end
    return ui
end

local function _loadWindUI()
    if _winduiCache then return _winduiCache end
    -- Return a Lua source that creates and returns the WindUI stub.
    -- This source is compiled and called by loadstring(...)() in the script.
    _winduiCache = [[
local ui = {}
ui._themes = {
    Dark = { Name="Dark", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(255,255,255),
        Background=Color3.fromRGB(30,30,30), BorderColor=Color3.fromRGB(50,50,50) },
    Light = { Name="Light", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(0,0,0),
        Background=Color3.fromRGB(255,255,255), BorderColor=Color3.fromRGB(200,200,200) },
    Graphite = { Name="Graphite", Accent=Color3.fromRGB(80,80,80), TextColor=Color3.fromRGB(255,255,255),
        Background=Color3.fromRGB(20,20,20), BorderColor=Color3.fromRGB(40,40,40) },
}
local function makeElement()
    local el = { __type="WindUIElement", _flags={}, Enabled=false, Value=false }
    function el:Toggle(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or false
        sub.Enabled = opts.Value or false
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, opts.Value) end
        return sub
    end
    function el:Slider(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or opts.Min or 0
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Dropdown(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or opts.Default or ""
        sub.Options = opts.Options or {}
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Colorpicker(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or Color3.fromRGB(255, 255, 255)
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Button(opts)
        opts = opts or {}
        if opts.Callback then pcall(opts.Callback) end
        return makeElement()
    end
    function el:Input(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or ""
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Paragraph(opts) return makeElement() end
    function el:Divider() return makeElement() end
    function el:Select(opts) return el:Dropdown(opts) end
    function el:Keybind(opts) return el:Toggle(opts) end
    function el:SetValue(v) self.Value = v; if self._cb then pcall(self._cb, v) end end
    function el:SetEnabled(v) self.Enabled = v end
    return el
end
local function makeWindow()
    local w = makeElement()
    w._flags = {}
    w._tabs = {}
    function w:Tab(opts)
        opts = opts or {}
        local t = makeElement()
        t._flags = {}
        t.Title = opts.Title or ""
        t.Icon = opts.Icon
        table.insert(w._tabs, t)
        return t
    end
    function w:GetFlag(name) return w._flags[name] end
    function w:GetFlagElement(name) return w._flags[name] end
    function w:ListFlags() return w._flags end
    function w:Open() end
    function w:Close() end
    function w:OnDestroy(fn) end
    function w:SetBackgroundImage(img) end
    function w:SetBackgroundImageTransparency(val) end
    function w:SetIconSize(size) end
    function w:Destroy() end
    return w
end
function ui:CreateWindow(opts) return makeWindow() end
function ui:GetThemes() return ui._themes end
function ui:SetTheme(name) end
function ui:AddTheme(theme) if theme and theme.Name then ui._themes[theme.Name] = theme end end
function ui:SetLanguage(lang) end
function ui:Localization(opts) end
function ui:Notify(opts) end
function ui:Save() end
function ui:Load() end
function ui:SetIconSize(size) end
return ui
]]
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
function module.Init(r, name) end
function module.Icon(name, opts, h)
    -- WindUI expects Icon to return {imageString, {ImageRectSize=..., ImageRectPosition=...}}
    -- when h ~= false, or just the image string when h == false
    h = h ~= false
    local img = 'rbxasset://textures/ui/GuiImagePlaceholder.png'
    local rect = { ImageRectSize = Vector2.new(0, 0), ImageRectPosition = Vector2.new(0, 0) }
    if h then
        return { img, rect }
    end
    return img
end
function module.GetIcon(name) return module.Icon(name, nil, false) end
function module.Icon2(name, opts) return module.Icon(name, opts, true) end
module.IconsType = iconType
module.Icons = {}
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
    uis._signals = { InputBegan = signal(), InputChanged = signal(), InputEnded = signal(),
        JumpRequest = signal(), TextBoxFocused = signal(), TextBoxFocusReleased = signal(),
        WindowFocusReleased = signal(), WindowFocused = signal(),
        TouchStarted = signal(), TouchEnded = signal(), TouchMoved = signal() }
    uis.InputBegan = uis._signals.InputBegan
    uis.InputChanged = uis._signals.InputChanged
    uis.InputEnded = uis._signals.InputEnded
    uis.JumpRequest = uis._signals.JumpRequest
    uis.TextBoxFocused = uis._signals.TextBoxFocused
    uis.TextBoxFocusReleased = uis._signals.TextBoxFocusReleased
    uis.WindowFocusReleased = uis._signals.WindowFocusReleased
    uis.WindowFocused = uis._signals.WindowFocused
    uis.TouchStarted = uis._signals.TouchStarted
    uis.TouchEnded = uis._signals.TouchEnded
    uis.TouchMoved = uis._signals.TouchMoved
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

    -- Deterministic local player simulation for headless compatibility tests.
    -- This never connects to Roblox or mutates an external game session.
    lumora = lumora or {}
    local function makeSimCharacter(player, position)
        local character = Instance.new("Model")
        character.Name = player.Name .. "Character"
        local root = Instance.new("Part")
        root.Name = "HumanoidRootPart"
        root.Size = Vector3.new(2, 2, 1)
        root.Position = position or Vector3.new(0, 5, 0)
        root.CFrame = CFrame.new(root.Position)
        root.Parent = character
        local head = Instance.new("Part")
        head.Name = "Head"
        head.Size = Vector3.new(2, 1, 1)
        head.Position = (position or Vector3.new(0, 5, 0)) + Vector3.new(0, 2, 0)
        head.CFrame = CFrame.new(head.Position)
        head.Parent = character
        local humanoid = _makeHumanoid and _makeHumanoid() or Instance.new("Humanoid")
        humanoid.Name = "Humanoid"
        humanoid.Parent = character
        character.Parent = player
        return character
    end

    function lumora.simulatePlayers(specs)
        specs = specs or {}
        local list = players._playerList or { players.LocalPlayer }
        for index, spec in ipairs(specs) do
            spec = spec or {}
            local simulated = Instance.new("Player")
            simulated.Name = spec.Name or ("SimPlayer" .. index)
            simulated.DisplayName = spec.DisplayName or simulated.Name
            simulated.UserId = spec.UserId or (90000000 + index)
            simulated.Team = spec.Team
            simulated.Backpack = Instance.new("Backpack", simulated)
            simulated.PlayerGui = Instance.new("PlayerGui", simulated)
            simulated.PlayerScripts = Instance.new("PlayerScripts", simulated)
            simulated._signals = { CharacterAdded = signal(), CharacterRemoving = signal(), CharacterAppearanceLoaded = signal() }
            simulated.CharacterAdded = simulated._signals.CharacterAdded
            simulated.CharacterRemoving = simulated._signals.CharacterRemoving
            simulated.CharacterAppearanceLoaded = simulated._signals.CharacterAppearanceLoaded
            function simulated:GetMouse()
                return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0), Target = nil,
                    Origin = Vector3.new(0,0,0), UnitRay = Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1)) }
            end
            simulated.Character = makeSimCharacter(simulated, spec.Position or Vector3.new(index * 6, 5, 0))
            table.insert(list, simulated)
            players._playerList = list
            players.PlayerAdded:Fire(simulated)
        end
        players._playerList = list
        return list
    end

    function lumora.resetSimulatedPlayers()
        local keep = players.LocalPlayer
        for _, player in ipairs(players._playerList or {}) do
            if player ~= keep then
                players.PlayerRemoving:Fire(player)
                player:Destroy()
            end
        end
        players._playerList = { keep }
    end
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
    function players:GetUserThumbnailAsync(userId, thumbType, thumbSize)
        return "rbxasset://textures/ui/GuiImagePlaceholder.png", true
    end
    function players:GetCharacterAppearanceAsync(userId)
        return { andThen = function(self, fn) return fn() end }
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
    -- Player signals
    lp._signals = { CharacterAdded = signal(), CharacterRemoving = signal(),
        CharacterAppearanceLoaded = signal() }
    lp.CharacterAdded = lp._signals.CharacterAdded
    lp.CharacterRemoving = lp._signals.CharacterRemoving
    lp.CharacterAppearanceLoaded = lp._signals.CharacterAppearanceLoaded
    -- Create a Character for the LocalPlayer
    local char = Instance.new("Model")
    char.Name = "LocalPlayerCharacter"
    local hrp = Instance.new("Part")
    hrp.Name = "HumanoidRootPart"
    hrp.Size = Vector3.new(2, 2, 1)
    hrp.CFrame = CFrame.new(0, 5, 0)
    hrp.Velocity = Vector3.zero
    hrp.Anchored = false
    hrp.CanCollide = true
    hrp.Parent = char
    local head = Instance.new("Part")
    head.Name = "Head"
    head.Size = Vector3.new(2, 1, 1)
    head.CFrame = CFrame.new(0, 7, 0)
    head.Parent = char
    local humanoid = _makeHumanoid and _makeHumanoid() or Instance.new("Humanoid")
    humanoid.Name = "Humanoid"
    humanoid.Parent = char
    -- Humanoid signals
    humanoid._signals = humanoid._signals or {}
    humanoid._signals.StateChanged = signal()
    humanoid.StateChanged = humanoid._signals.StateChanged
    humanoid._signals.HealthChanged = signal()
    humanoid.HealthChanged = humanoid._signals.HealthChanged
    humanoid._signals.Died = signal()
    humanoid.Died = humanoid._signals.Died
    char.Parent = lp
    lp.Character = char
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

-- Debris service
do
    local debris = game:GetService("Debris")
    function debris:AddItem(item, lifetime) end
    function debris:SetLegacyMaxItems(maxItems) end
end

-- LocalizationService
do
    local ls = game:GetService("LocalizationService")
    ls.SystemLocaleId = "en"
end

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
// that general Roblox scripts rely on.
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
