local optional: number? = 4
local total = optional or 0
total += 2
for i = 1, 2 do
    if i == 1 then continue end
    total += i
end
assert(total == 8)
assert(bit32.band(7, 3) == 3)
local packed = string.pack("I2", 513)
assert(string.unpack("I2", packed) == 513)
assert(buffer.create(4) ~= nil)
assert(type(debug.info(1, "n")) == "string" or debug.info(1, "n") == nil)
print("luau-modern-ok")
