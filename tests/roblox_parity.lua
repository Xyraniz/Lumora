-- Lumora parity test: asserts the deliberate divergences from the official
-- Luau CLI that match real Roblox instead (globals writable, library tables
-- frozen, Roblox globals present). Runs in Roblox mode (default; no flags).

local failures = {}
local function check(name, cond)
    if cond then print("ok   " .. name) else print("FAIL " .. name); table.insert(failures, name) end
end

-- 1. Globals environment is writable (Roblox parity; official CLI freezes it)
local writeOk = pcall(function() ParityGlobal = 1 end)
check("globals-writable", writeOk and ParityGlobal == 1)
local gWriteOk = pcall(function() _G.ParityGlobal2 = 2 end)
check("_G-writable", gWriteOk and _G.ParityGlobal2 == 2)

-- 2. Builtin library tables are frozen (Roblox + official CLI parity)
for _, lib in ipairs({"string", "table", "math", "os", "coroutine", "debug", "utf8", "bit32"}) do
    local ok = pcall(function() _G[lib].parity_probe = 1 end)
    check("lib-frozen-" .. lib, not ok)
end

-- 3. String metatable is frozen
local mtOk = pcall(function() getmetatable("").__index = nil end)
check("strmeta-frozen", not mtOk)

-- 4. Library functions still work normally (frozen != broken)
check("string-rep", ("ab"):rep(3) == "ababab")
check("string-format", string.format("%.2f", 0.5) == "0.50")
check("math-clamp", math.clamp(5, 0, 3) == 3)
check("table-concat", table.concat({1, 2, 3}, "-") == "1-2-3")

-- 5. Roblox globals exist and behave
check("game-present", typeof(game) == "Instance")
check("workspace-present", typeof(workspace) == "Instance")
check("instance-new", typeof(Instance) == "table")
local part = Instance.new("Part")
check("instance-create", typeof(part) == "Instance")
check("instance-parent", (pcall(function() part.Parent = workspace end)) and part.Parent == workspace)

-- 6. task scheduler still works with frozen libraries
local ran = false
task.spawn(function() ran = true end)
check("task-spawn", ran)

-- 7. loadstring has Roblox semantics (present, compiles, errors safely)
check("loadstring-present", type(loadstring) == "function")
local fn = loadstring("return 7 * 6")
check("loadstring-compiles", fn and fn() == 42)
local okl, errl = loadstring("syntax ((")
check("loadstring-rejects-bad", okl == nil and type(errl) == "string")

if #failures > 0 then
    error(("parity failures: %d (%s)"):format(#failures, table.concat(failures, ", ")))
end
print("roblox-parity-ok")
