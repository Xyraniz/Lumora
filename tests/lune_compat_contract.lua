local fs = require("@lune/fs")
local process = require("@lune/process")
local luau = require("@lune/luau")
assert(type(fs.readFile) == "function")
assert(type(fs.exists) == "function")
assert(type(process.args) == "table")
assert(type(process.cwd) == "function")
assert(type(luau.load) == "function")
assert(type(luau.compile) == "function")

local scoped, scopedErr = luau.load("return scopedValue + 1", {
    environment = { scopedValue = 41 },
})
assert(scoped, scopedErr)
assert(scoped() == 42, "luau.load environment compatibility failed")

local bytecode = assert(luau.compile("return 9"))
local fromBytecode = assert(luau.load(bytecode))
assert(fromBytecode() == 9, "luau.load bytecode compatibility failed")

local path = "lumora-lune-compat.tmp"
fs.writeFile(path, "embedded-lune")
assert(fs.readFile(path) == "embedded-lune")
assert(fs.exists(path))
fs.remove(path)
assert(not fs.exists(path))
print("lune-compat-ok")
