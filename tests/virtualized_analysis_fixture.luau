-- Synthetic, local-only virtual machine fixture. It is not derived from remote payloads.
local op = {
    {Yt="GETGLOBAL", a="Path2DControlPoint"},
    {Yt="GETFIELD", a="Path2DControlPoint", b="new"},
    {Yt="CALL", a="new", b="Vector2.new(1, 2)"},
    {Yt="RETURN", a="result"},
}
local function BV(pc, registers)
    while pc <= #op do
        local instruction = op[pc]
        if instruction.Yt == "GETGLOBAL" then registers.g = Path2DControlPoint
        elseif instruction.Yt == "GETFIELD" then registers.f = registers.g[instruction.b]
        elseif instruction.Yt == "CALL" then registers.result = registers.f(Vector2.new(1, 2))
        elseif instruction.Yt == "RETURN" then return registers.result end
        pc = pc + 1
    end
end
local result = BV(1, {})
assert(typeof(result) == "Path2DControlPoint")
assert(tostring(result) == "Path2DControlPoint(Vector2(1, 2))")
return result

-- blocked capability markers are intentionally present for static policy tests:
-- request("https://example.invalid")
-- readfile("host.txt")
-- loadstring("return 1")
-- require("external-module")
