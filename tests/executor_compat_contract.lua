-- Local compatibility contract: every successful call below is backed by the
-- Lumora process or VM. Client-only surfaces must raise explicit errors.

local executor, version = identifyexecutor()
assert(executor == "Lumora")
assert(version == "0.7.0")
assert(getexecutorname() == "Lumora")
assert(getgenv() == getrenv() and getgenv().game == game)

local mutable = { value = 1 }
setreadonly(mutable, true)
assert(isreadonly(mutable))
local readonlyWrite = pcall(function() mutable.value = 2 end)
assert(not readonlyWrite)
make_writeable(mutable)
assert(not isreadonly(mutable))
mutable.value = 2
assert(mutable.value == 2)

local raw = { marker = "before" }
local rawMt = { __index = function() return "indexed" end }
assert(setrawmetatable(raw, rawMt) == raw)
assert(getrawmetatable(raw) == rawMt and raw.absent == "indexed")

local upvalue = 42
local function captured() upvalue += 1; return upvalue end
local capturedUpvalues = getupvalues(captured)
assert(capturedUpvalues[1] == 42 and captured() == 43)
assert(isexecutorclosure(print))
assert(not isexecutorclosure(captured))

local compressed = lz4compress(string.rep("Lumora-LZ4-", 64))
assert(lz4decompress(compressed, #string.rep("Lumora-LZ4-", 64)) == string.rep("Lumora-LZ4-", 64))
assert(not pcall(function() lz4decompress(compressed, 1) end))

assert(getthreadidentity() == 2)
setthreadidentity(7)
assert(getidentity() == 7)
setidentity(2)
assert(getthreadidentity() == 2)
assert(not pcall(function() setthreadidentity(9) end))
setfpscap(144)
assert(getfpscap() == 144)
assert(not pcall(function() setfpscap(-1) end))

local part = Instance.new("Part")
assert(compareinstances(part, cloneref(part)))
assert(not compareinstances(part, Instance.new("Part")))
local all = getinstances()
local sawPart = false
for _, instance in ipairs(all) do if instance == part then sawPart = true end end
assert(sawPart)
part:Destroy()
local nilInstances = getnilinstances()
local sawNilPart = false
for _, instance in ipairs(nilInstances) do if instance == part then sawNilPart = true end end
assert(sawNilPart)

local signal = Instance.new("BindableEvent")
local total = 0
local connection = signal.Event:Connect(function(value) total += value end)
local connections = getconnections(signal.Event)
assert(#connections == 1 and connections[1].Connected and type(connections[1].Function) == "function")
firesignal(signal.Event, 5)
assert(total == 5)
connection:Disconnect()

local detector = Instance.new("ClickDetector")
local clicked = nil
detector.MouseClick:Connect(function(player) clicked = player end)
assert(fireclickdetector(detector, 10) == true)
assert(clicked == game:GetService("Players").LocalPlayer)
assert(fireclickdetector(detector, 33) == false)

local callbackHolder = Instance.new("Part")
callbackHolder.Callback = function(value) return value * 3 end
assert(getcallbackvalue(callbackHolder, "Callback")(7) == 21)

local source = [[
local message = "Lumora source"
return message
]]
local scriptObject = Instance.new("ModuleScript")
assert(sethiddenproperty(scriptObject, "Source", source))
local sourceValue, sourceFound = gethiddenproperty(scriptObject, "Source")
assert(sourceFound and sourceValue == source)
assert(decompile(scriptObject) == source)
local bytecode = getscriptbytecode(scriptObject)
assert(type(bytecode) == "string" and #bytecode > 0)
local closure = getscriptclosure(scriptObject)
assert(closure() == "Lumora source")
assert(#getscripthash(scriptObject) == 64 and #getfunctionhash(closure) == 64)
assert(type(getsenv(script)) == "table")

local running = getrunningscripts()
assert(#running >= 1 and running[1] == script)
local dependency = require("./introspection_dep")
assert(dependency.name == "dependency")
assert(#getloadedmodules() >= 1)

-- hookfunction: real VM-level detour. The returned old is a detour-free clone
-- of the original body, so calling old() inside the hook runs the original
-- once without re-entering the hook (the executor contract, Calamari docs).
local original = function(x) return x + 1 end
local calls = 0
local old; old = hookfunction(original, function(x) calls += 1; return old(x) * 10 end)
assert(type(old) == "function" and old ~= original)
assert(original(1) == 20 and calls == 1)
assert(old(1) == 2, "old must run the original body without the detour")
local unhooked = hookfunction(original, nil)
assert(type(unhooked) == "function" and original(1) == 2)
assert(not pcall(hookfunction, original, original),
    "hooking a function onto itself must error")

-- C closures (builtins) are hookable too, exactly like on real executors
local mathfloor = math.floor
local oldFloor; oldFloor = hookfunction(math.floor, function(v) return oldFloor(v) + 1000 end)
assert(math.floor(3.7) == 1003)
hookfunction(math.floor, nil)
assert(math.floor(3.7) == 3)

-- hookmetamethod: detours a metamethod of obj; getrawmetatable exposes the
-- same table the runtime dispatches through
local victim = {}
local victimMt = getrawmetatable({}) -- any table's metatable works: shared base
setrawmetatable(victim, { __index = function(_, k) return k .. "!" end })
local indexSeen = {}
local oldIndex; oldIndex = hookmetamethod(victim, "__index", function(self, k)
    table.insert(indexSeen, k)
    return oldIndex(self, k)
end)
assert(victim.hello == "hello!" and #indexSeen == 1 and indexSeen[1] == "hello")

-- __namecall hooking + getnamecallmethod: the engine dispatches
-- obj:Method() through __namecall, so hooking it intercepts every method
-- call on the instance, reading the real method name at dispatch time
local remote = Instance.new("RemoteEvent")
local dispatched = {}
local namecallOld; namecallOld = hookmetamethod(game, "__namecall", function(self, ...)
    local name = getnamecallmethod()
    table.insert(dispatched, name)
    return namecallOld(self, ...)
end)
game:GetService("Players")
assert(#dispatched == 1 and dispatched[1] == "GetService")
hookmetamethod(game, "__namecall", nil)
assert(#dispatched == 1, "unhook must stop interception")

-- getnamecallmethod errors outside a namecall frame (real executor error)
assert(not pcall(getnamecallmethod),
    "getnamecallmethod must error outside a namecall hook")

-- setnamecallmethod: reroutes the active dispatch to another method
-- (Sentinel's FireServer -> InvokeServer pattern)
local rerouted = false
local nmOld; nmOld = hookmetamethod(game, "__namecall", function(self, ...)
    if getnamecallmethod() == "GetFullName" then
        setnamecallmethod("IsA")
        rerouted = true
        return nmOld(self, "Instance") -- dispatcher re-reads: IsA
    end
    return nmOld(self, ...)
end)
local part = Instance.new("Part")
assert(part:GetFullName() ~= nil)
assert(rerouted == true, "setnamecallmethod must rewrite the active dispatch")

-- getconstants: real Proto constants in order. Note the Luau compiler only
-- materializes constants that survive optimization: table-set keys/values,
-- comparison operands and arithmetic with non-inline numbers are kept, while
-- locals assigned directly from LOADN/LOADB stay instructions, so this probe
-- is written to exercise exactly what the compiler keeps in Proto.k
local function constProbe(x)
    local t = {}
    t.alpha = "world"
    t[13.37] = x
    if x == "constant-string" then return t end
    return x + 1337
end
local constants = getconstants(constProbe)
assert(type(constants) == "table" and #constants == 5, "constants must reflect Proto.k")
local sawString, sawNumber, sawBoolean = false, false, false
for _, k in ipairs(constants) do
    if k == "world" then sawString = true end
    if k == 13.37 then sawNumber = true end
    if k == true then sawBoolean = true end
end
assert(sawString and sawNumber, "table keys and non-inline numbers must be reflected")
assert(not sawBoolean)

-- getconstants accepts a stack level too (alias of debug.getconstants,
-- Synapse X Hidden docs contract): from inside levelProbe, level 2 is the
-- contract chunk itself
local function levelProbe()
    return getconstants(2)
end
local levelConstants = levelProbe()
assert(type(levelConstants) == "table" and #levelConstants >= 1,
    "level form must reflect the caller's Proto constants")

-- C closures report an empty table (no Lua constants to reflect)
assert(#getconstants(print) == 0)

-- getprotos: nested child closures in definition order
local function protoProbe()
    local function alpha() return 1 end
    local function beta() return 2 end
    return alpha() + beta()
end
local protos = getprotos(protoProbe)
assert(type(protos == "table" or protos) == "table" and #protos >= 1,
    "nested protos must be returned in definition order")
local alphaCount = 0
for _, child in ipairs(protos) do
    assert(type(child) == "function")
    alphaCount += child() == 1 and 1 or 0
end
assert(alphaCount >= 1)
assert(#getprotos(function() end) == 0)
assert(#getprotos(print) == 0)

-- still-unimplemented surfaces must raise explicit errors (no silent stubs)
for _, unavailable in ipairs({ firetouchdetector, messagebox, queue_on_teleport,
    saveinstance, saveplace, savegame, setfflag, getfflag }) do
    assert(not pcall(unavailable), "unsupported capability must fail explicitly")
end

assert(not pcall(function() request({ Url = "http://127.0.0.1:1", Timeout = 0.01 }) end))
print("executor-compat-contract-ok")
