local first = require("./require_module")
local second = require("./require_module")
assert(first == second, "require should cache module results")
assert(first.value == 42, "relative module should return its value")
assert(first.chunk:find("require_module", 1, true), "module should receive a useful chunk name")
assert(first.robloxType == "Instance", "Roblox modules should share Lumora globals")

local package = require("./require_package")
assert(package.name == "init-module", "require should resolve init.luau")
assert(package.nested == 7, "init module should execute normally")

local ok, errorMessage = pcall(function()
    require("./missing_module")
end)
assert(not ok and tostring(errorMessage):find("could not resolve child component", 1, true),
    "missing modules should fail with a useful error")

print("require-contract-ok")
