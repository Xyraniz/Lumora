local process = require("@lumora/process")
local system = require("@lumora/system")
assert(type(process.pid()) == "number" and process.pid() > 0)
assert(type(process.execPath()) == "string" and #process.execPath() > 0)
-- The shell differs per platform: POSIX hosts have /bin/sh, Windows has
-- cmd.exe. Both branches emit "out" on stdout and "err" on stderr with no
-- trailing newline so the captured streams compare exactly.
local result
if system.os == "windows" then
    result = process.run("cmd.exe", {"/c", "<nul set /p=out & <nul set /p=err 1>&2"})
else
    result = process.run("sh", {"-c", "printf out; printf err >&2"})
end
assert(result.ok and result.code == 0 and result.stdout == "out" and result.stderr == "err")

local crypto = require("@lumora/crypto")
local key = crypto.secretbox.keygen()
local nonce = crypto.randomBytes(crypto.secretbox.nonceBytes)
local sealed = crypto.secretbox.seal("lumora", nonce, key)
assert(crypto.secretbox.open(sealed, nonce, key) == "lumora")
local password = crypto.password.hash("correct horse battery staple")
assert(crypto.password.verify("correct horse battery staple", password))
assert(not crypto.password.verify("wrong", password))

local serde = require("@lumora/serde")
local parsed = serde.tomlDecode('title = "Lumora"\nenabled = true\n[server]\nport = 8080\ntags = ["luau", "roblox"]\n')
assert(parsed.title == "Lumora" and parsed.enabled and parsed.server.port == 8080 and parsed.server.tags[2] == "roblox")

local datetime = require("@lumora/datetime")
local ts = datetime.fromIsoDate("2024-01-02T03:04:05")
assert(type(ts) == "number" and datetime.toUnix(datetime.fromUnix(ts)) == ts)
assert(datetime.toIsoDate(ts):match("^2024%-01%-02T03:04:05Z$"))

local fs = require("@lumora/fs")
fs.writeDir("typed")
fs.writeFile("typed/a", "x")
local entries = fs.listDir and fs.listDir("typed")
if entries then assert(entries[1].name == "a" and entries[1].type == "file") end
print("expanded-native-contract-ok")
