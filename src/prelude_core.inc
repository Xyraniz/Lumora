R"LUA(
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
        if old ~= value and self.Changed then self.Changed:Fire(key) end
        return
    end
    local old = rawget(self, key)
    rawset(self, key, value)
    local signals = rawget(self, "_propSignals")
    if signals and old ~= value and signals[key] then signals[key]:Fire(value) end
    if old ~= value and self.Changed then self.Changed:Fire(key) end
end

Instance = {}
function Instance.new(className, parent)
    local o = setmetatable({ __type="Instance", _properties={
        ClassName=className, Name=className, AttributeChanged=signal(),
        ChildAdded=signal(), ChildRemoved=signal(), Changed=signal(),
        MouseEnter=signal(), MouseLeave=signal(), MouseButton1Down=signal(),
        MouseButton1Up=signal(), MouseButton1Click=signal(), Activated=signal(),
        InputBegan=signal(), InputChanged=signal(), InputEnded=signal(),
        Focused=signal(), FocusLost=signal()
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
-- Deterministic cooperative scheduler. Time is virtual and advances only while
-- the scheduler is running, so delayed work is real and testable without
-- sleeping the host process. A task is resumed only after its wake time.
tasklib._threads = {}
tasklib._maxCycles = 50
tasklib._cycleCount = 0
tasklib._errors = {}
tasklib._now = 0
tasklib._trace = {}
tasklib._traceEnabled = true

local function traceTask(event, co, record, detail)
    if not tasklib._traceEnabled then return end
    table.insert(tasklib._trace, { event = event, time = tasklib._now,
        cycle = tasklib._cycleCount, thread = tostring(co),
        wakeAt = record and record.wakeAt or tasklib._now, detail = detail or "" })
end

local function rememberTaskError(err)
    table.insert(tasklib._errors, tostring(err))
end

local function makeTaskThread(routine)
    if type(routine) == "thread" then return routine end
    assert(type(routine) == "function", "task routine must be a function or thread")
    return coroutine.create(routine)
end

local function enqueue(co, args, wakeAt, readyCycle)
    tasklib._threads[co] = {
        args = args or table.pack(),
        wakeAt = wakeAt or tasklib._now,
        readyCycle = readyCycle or tasklib._cycleCount,
        waiting = false,
    }
end

local function resumeTask(co, record, resumeArgs)
    if coroutine.status(co) == "dead" then
        tasklib._threads[co] = nil
        return false
    end
    local args = resumeArgs or record.args or table.pack()
    local ok, yielded = coroutine.resume(co, table.unpack(args, 1, args.n))
    if not ok then
        traceTask("error", co, record, tostring(yielded))
        rememberTaskError(yielded)
        tasklib._threads[co] = nil
        return false
    end
    if coroutine.status(co) == "dead" then
        traceTask("complete", co, record)
        tasklib._threads[co] = nil
        return true
    end
    local duration = 0
    if type(yielded) == "table" and yielded.__task_wait then
        duration = math.max(0, tonumber(yielded.duration) or 0)
    end
    record.args = table.pack(duration)
    record.wakeAt = tasklib._now + duration
    record.readyCycle = tasklib._cycleCount + 1
    record.waiting = true
    traceTask("yield", co, record, tostring(duration))
    return true
end

function tasklib.spawn(fn, ...)
    local co = makeTaskThread(fn)
    local record = { args = table.pack(...), wakeAt = tasklib._now, readyCycle = tasklib._cycleCount, waiting = false }
    tasklib._threads[co] = record
    traceTask("spawn", co, record)
    resumeTask(co, record)
    return co
end

function tasklib.defer(fn, ...)
    local co = makeTaskThread(fn)
    enqueue(co, table.pack(...), tasklib._now, tasklib._cycleCount + 1)
    traceTask("defer", co, tasklib._threads[co])
    -- At top level there is no host frame that will naturally advance the
    -- cooperative loop, so perform exactly one zero-time scheduler turn.
    -- Inside a running task, the readyCycle boundary preserves next-cycle
    -- semantics and prevents re-entrant execution.
    if not tasklib._inScheduler then tasklib._step(0) end
    return co
end

function tasklib.delay(seconds, fn, ...)
    assert(type(seconds) == "number", "task.delay expects seconds as a number")
    local co = makeTaskThread(fn)
    enqueue(co, table.pack(...), tasklib._now + math.max(0, seconds), tasklib._cycleCount)
    traceTask("delay", co, tasklib._threads[co], tostring(seconds))
    return co
end

function tasklib.resume(co, ...)
    assert(type(co) == "thread", "task.resume expects a thread")
    local record = tasklib._threads[co]
    if not record then
        -- task.resume also accepts a coroutine created directly with
        -- coroutine.create, not only handles returned by task.spawn/delay.
        if coroutine.status(co) ~= "dead" then
            record = { args = table.pack(...), wakeAt = tasklib._now,
                readyCycle = tasklib._cycleCount, waiting = false }
            tasklib._threads[co] = record
            resumeTask(co, record)
        end
        return co
    end
    record.wakeAt = tasklib._now
    record.readyCycle = tasklib._cycleCount
    resumeTask(co, record, table.pack(...))
    return co
end

function tasklib.deferSelf()
    coroutine.yield({ __task_wait = true, duration = 0 })
end

function tasklib.cancel(co)
    assert(type(co) == "thread", "task.cancel expects a thread")
    local record = tasklib._threads[co]
    if record then
        traceTask("cancel", co, record)
        tasklib._threads[co] = nil
        pcall(coroutine.close, co)
    end
end

function tasklib.status(co)
    assert(type(co) == "thread", "task.status expects a thread")
    if not tasklib._threads[co] then return coroutine.status(co) end
    return "suspended"
end

function tasklib.wait(seconds)
    seconds = math.max(0, tonumber(seconds) or 0)
    local elapsed = coroutine.yield({ __task_wait = true, duration = seconds })
    return tonumber(elapsed) or seconds
end

function tasklib._step(delta)
    tasklib._inScheduler = true
    delta = math.max(0, tonumber(delta) or (1 / 60))
    tasklib._now += delta
    tasklib._cycleCount += 1
    local progressed = false
    local ready = {}
    for co, record in pairs(tasklib._threads) do
        if coroutine.status(co) == "dead" then
            tasklib._threads[co] = nil
        elseif record.wakeAt <= tasklib._now and record.readyCycle <= tasklib._cycleCount then
            table.insert(ready, { co = co, record = record })
        end
    end
    table.sort(ready, function(a, b) return tostring(a.co) < tostring(b.co) end)
    for _, item in ipairs(ready) do
        if tasklib._threads[item.co] == item.record then
            progressed = true
            resumeTask(item.co, item.record)
        end
    end
    tasklib._inScheduler = false
    return progressed
end

function tasklib._runScheduler()
    for _ = 1, tasklib._maxCycles do
        local nextWake = nil
        for _, record in pairs(tasklib._threads) do
            if not nextWake or record.wakeAt < nextWake then nextWake = record.wakeAt end
        end
        if not nextWake then break end
        local delta = math.max(1 / 60, nextWake - tasklib._now)
        tasklib._step(delta)
    end
    local errors = tasklib._errors
    tasklib._errors = {}
    return #errors == 0, errors
end

function tasklib.trace(enabled)
    if enabled ~= nil then tasklib._traceEnabled = not not enabled end
    return tasklib._traceEnabled
end
function tasklib.traceEvents(clear)
    local result = tasklib._trace
    if clear then tasklib._trace = {} end
    return result
end
function tasklib.clearTrace() tasklib._trace = {} end

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
syn.request = syn.request or function()
    error("syn.request is unavailable: Lumora has no network transport")
end

-- request / http.request: fail loudly rather than returning a false 200.
local function _stubRequest(opts)
    error("HTTP requests are unavailable: Lumora has no network transport")
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
local function _fsReadDir(path)
    local normalized = path or ""
    if normalized ~= "" then normalized = _normalizePath(normalized) end
    local prefix = normalized == "" and "" or normalized .. "/"
    local result, seen = {}, {}
    local function add(name)
        if name and not seen[name] then seen[name] = true; table.insert(result, name) end
    end
    for file in pairs(_files) do
        if file:sub(1, #prefix) == prefix then add(file:sub(#prefix + 1):match("^[^/]+")) end
    end
    for folder in pairs(_folders) do
        if folder ~= "" and folder:sub(1, #prefix) == prefix then add(folder:sub(#prefix + 1):match("^[^/]+")) end
    end
    table.sort(result)
    return result
end
local function _fsMetadata(path)
    local normalized = _normalizePath(path)
    if _files[normalized] ~= nil then
        return { exists = true, isFile = true, isDir = false, size = #_files[normalized] }
    end
    if _folders[normalized] then return { exists = true, isFile = false, isDir = true, size = 0 } end
    return { exists = false, isFile = false, isDir = false, size = 0 }
end
local function _fsCopy(from, to)
    from, to = _normalizePath(from), _normalizePath(to)
    if _files[from] ~= nil then _validateParent(to); _files[to] = _files[from]; return end
    assert(_folders[from], "path does not exist: " .. from)
    _ensureFolders(to)
    local prefix = from .. "/"
    for file, content in pairs(_files) do
        if file:sub(1, #prefix) == prefix then
            local destination = to .. "/" .. file:sub(#prefix + 1)
            _validateParent(destination); _files[destination] = content
        end
    end
end
local function _fsMove(from, to)
    from, to = _normalizePath(from), _normalizePath(to)
    _fsCopy(from, to)
    if _files[from] ~= nil then _files[from] = nil else delfolder(from) end
end
local function _fsRemove(path)
    local normalized = _normalizePath(path)
    if _files[normalized] ~= nil then _files[normalized] = nil; return end
    if _folders[normalized] then delfolder(normalized); return end
    error("path does not exist: " .. normalized)
end
lumora = lumora or {}
lumora.fs = {
    readFile = readfile, writeFile = writefile, appendFile = appendfile,
    readDir = _fsReadDir, writeDir = makefolder, makeDir = makefolder,
    removeFile = delfile, removeDir = delfolder, remove = _fsRemove,
    isFile = isfile, isDir = isfolder, metadata = _fsMetadata,
    copy = _fsCopy, move = _fsMove
}
function lumora.fs.snapshot()
    local result = { files = {}, folders = {} }
    for path, content in pairs(_files) do result.files[path] = content end
    for path, present in pairs(_folders) do if present then result.folders[path] = true end end
    return result
end
function lumora.fs.diff(before, after)
    assert(type(before) == "table" and type(after) == "table", "snapshots must be tables")
    local result = { added = {}, removed = {}, changed = {} }
    local oldFiles, newFiles = before.files or {}, after.files or {}
    for path, content in pairs(newFiles) do
        if oldFiles[path] == nil then table.insert(result.added, path)
        elseif oldFiles[path] ~= content then table.insert(result.changed, path) end
    end
    for path in pairs(oldFiles) do if newFiles[path] == nil then table.insert(result.removed, path) end end
    table.sort(result.added); table.sort(result.removed); table.sort(result.changed)
    return result
end
loadfile = function(path)
    path = _normalizePath(path)
    return loadstring(readfile(path), "@" .. path)
end

-- Resolve a virtual filesystem asset without pretending it is uploaded to
-- Roblox. The URI is stable and can be inspected by headless tests.
getcustomasset = function(path)
    path = _normalizePath(path)
    assert(_files[path] ~= nil, "asset does not exist: " .. path)
    return "rbxasset://lumora/" .. path
end
getsynasset = getcustomasset

-- getconnections: return empty list
getconnections = function(signal) return {} end

-- isluau: true in Lumora
isluau = function() return true end

-- ========== Drawing API ==========
local drawing_mt = { __metatable = "Drawing" }
drawing_mt.__index = drawing_mt
function drawing_mt:Remove() self.Visible = false end
function drawing_mt:Destroy() self.Visible = false end

Drawing = { _objects = {} }
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
    table.insert(Drawing._objects, obj)
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
            if key == "ToEulerAnglesXYZ" or key == "ToOrientation" then
                return function(cf)
                    -- Invert the matrix produced by CFrame.Angles(x, y, z).
                    -- Clamp the asin input to absorb composition round-off.
                    local function atan2(y, x)
                        if x > 0 then return math.atan(y / x) end
                        if x < 0 and y >= 0 then return math.atan(y / x) + math.pi end
                        if x < 0 and y < 0 then return math.atan(y / x) - math.pi end
                        if y > 0 then return math.pi / 2 end
                        if y < 0 then return -math.pi / 2 end
                        return 0
                    end
                    local sy = math.max(-1, math.min(1, cf.R0[3]))
                    local y = math.asin(sy)
                    local cy = math.cos(y)
                    if math.abs(cy) > 1e-7 then
                        return atan2(-cf.R1[3], cf.R2[3]), y,
                            atan2(-cf.R0[2], cf.R0[1])
                    end
                    -- At gimbal lock, use a stable representation with z=0.
                    return atan2(cf.R2[1], cf.R1[1]), y, 0
                end
            end
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

)LUA"
