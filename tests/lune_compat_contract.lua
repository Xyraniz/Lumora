local fs = require("@lune/fs")
local process = require("@lune/process")
local luau = require("@lune/luau")
assert(type(fs.readFile) == "function")
assert(type(fs.exists) == "function")
assert(type(fs.isFile) == "function")
assert(type(fs.isDir) == "function")
assert(type(fs.copy) == "function")
assert(type(fs.move) == "function")
assert(type(process.args) == "table")
assert(type(process.cwd) == "function")
assert(type(process.exec) == "function")
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
assert(fs.isFile(path) and not fs.isDir(path))
local copied = path .. ".copy"
fs.copy(path, copied)
assert(fs.readFile(copied) == "embedded-lune")
local moved = path .. ".moved"
fs.move(copied, moved)
assert(fs.isFile(moved) and not fs.exists(copied))
local result = process.exec("printf", {"lune-exec"})
assert(result.ok and result.code == 0 and result.stdout == "lune-exec")
fs.remove(path)
fs.remove(moved)
assert(not fs.exists(path))
print("lune-compat-ok")
