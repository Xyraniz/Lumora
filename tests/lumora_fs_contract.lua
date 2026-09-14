-- The native Lumora fs module must remain virtual, deterministic, and typed.
local fs = require("@lumora/fs")

fs.writeDir("fixtures/nested")
fs.writeFile("fixtures/nested/data.txt", "lumora")
fs.appendFile("fixtures/nested/data.txt", "-fs")
assert(fs.readFile("fixtures/nested/data.txt") == "lumora-fs", "read/write/append")

local metadata = fs.metadata("fixtures/nested/data.txt")
assert(metadata.exists and metadata.isFile and not metadata.isDir and metadata.size == 9, "file metadata")
assert(fs.metadata("fixtures/nested").isDir, "directory metadata")
local entries = fs.readDir("fixtures")
assert(#entries == 1 and entries[1] == "nested", "directory entries")

fs.copy("fixtures/nested", "copied")
assert(fs.readFile("copied/data.txt") == "lumora-fs", "recursive copy")
fs.move("copied/data.txt", "moved.txt")
assert(fs.isFile("moved.txt") and not fs.isFile("copied/data.txt"), "file move")
fs.remove("copied")
assert(not fs.metadata("copied").exists, "recursive remove")

print("lumora-fs-contract-ok")
