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

for _, unavailable in ipairs({ hookfunction, hookmetamethod, getnamecallmethod, setnamecallmethod,
    getconstants, getprotos, firetouchdetector, messagebox, queue_on_teleport,
    saveinstance, saveplace, savegame, setfflag, getfflag }) do
    assert(not pcall(unavailable), "unsupported capability must fail explicitly")
end

assert(not pcall(function() request({ Url = "http://127.0.0.1:1", Timeout = 0.01 }) end))
print("executor-compat-contract-ok")
