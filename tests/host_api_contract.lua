-- Host capabilities are deliberately local, deterministic, and never reach Roblox
-- or the host operating system.
assert(type(setclipboard) == "function")
assert(type(getclipboard) == "function")
setclipboard("lumora-clipboard")
assert(getclipboard() == "lumora-clipboard")

local caps = lumora.capabilities()
assert(caps.runtime == "Lumora")
assert(caps.clipboard == "memory")
assert(caps.filesystem == "memory")
assert(caps.network == "disabled")
assert(caps.executorHooks == false)
assert(caps.robloxClient == false)

writefile("fixtures/data.txt", "first")
appendfile("fixtures/data.txt", "-second")
writefile("fixtures/module.lua", "return 42")
assert(isfile("fixtures/data.txt"))
assert(isfolder("fixtures"))
assert(readfile("fixtures/data.txt") == "first-second")
local files = listfiles("fixtures")
assert(#files == 2 and files[1] == "fixtures/data.txt" and files[2] == "fixtures/module.lua")
local loaded = assert(loadfile("fixtures/module.lua"))
assert(type(loaded) == "function" and loaded() == 42)
delfile("fixtures/data.txt")
assert(not isfile("fixtures/data.txt"))
delfolder("fixtures")
assert(not isfolder("fixtures"))

local hs = game:GetService("HttpService")
local encoded = hs:JSONEncode({z = 3, a = true, items = {"one", 2, false}})
assert(encoded == '{"a":true,"items":["one",2,false],"z":3}', encoded)
local decoded = hs:JSONDecode(encoded)
assert(decoded.a == true and decoded.z == 3)
assert(decoded.items[1] == "one" and decoded.items[2] == 2 and decoded.items[3] == false)
assert(json.encode({answer = 42}) == '{"answer":42}')
assert(json.decode('{"ok":true}').ok == true)
local escaped = hs:JSONDecode('{"text":"line\\n\\u00e9"}')
assert(escaped.text == "line\né")
local validDecode = pcall(function() hs:JSONDecode('{"missing":}') end)
assert(not validDecode)
local cycle = {}
cycle.self = cycle
local validEncode = pcall(function() hs:JSONEncode(cycle) end)
assert(not validEncode)

local part = Instance.new("Part")
local changed = 0
part:GetPropertyChangedSignal("Name"):Connect(function(value)
    changed = changed + 1
    assert(value == "Renamed")
end)
part.Name = "Renamed"
part.Name = "Renamed"
assert(changed == 1)

local function inner()
    local frames = getcallstack(1, 16)
    assert(#frames > 0)
    local sawInner = false
    for _, frame in ipairs(frames) do
        if frame.name == "inner" then sawInner = true end
    end
    assert(sawInner)
end
inner()

print("host-api-contract-ok")
