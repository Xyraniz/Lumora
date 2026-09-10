local module = require("./require_module")
assert(module.value == 42, "pure Luau require should return module values")
assert(module.robloxType == "nil", "--no-roblox modules should not receive Roblox globals")
print("require-pure-contract-ok")
