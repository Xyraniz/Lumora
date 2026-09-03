-- Properties and methods tests for Vector2, Vector3, CFrame, Color3, UDim2.
-- Validates that properties return correct values and that methods are
-- callable both with dot and colon syntax where appropriate.

-- ===== Vector3 properties =====
local v = Vector3.new(3, 4, 0)
assert(math.abs(v.Magnitude - 5) < 1e-9, "Vector3 Magnitude should be 5")
local unit = v.Unit
assert(math.abs(unit.Magnitude - 1) < 1e-9, "Vector3 Unit Magnitude should be 1")
assert(math.abs(unit.X - 0.6) < 1e-9, "Vector3 Unit X")
assert(math.abs(unit.Y - 0.8) < 1e-9, "Vector3 Unit Y")

-- Vector3 arithmetic
local sum = Vector3.new(1, 2, 3) + Vector3.new(4, 5, 6)
assert(sum.X == 5 and sum.Y == 7 and sum.Z == 9, "Vector3 addition")
local diff = Vector3.new(10, 20, 30) - Vector3.new(1, 2, 3)
assert(diff.X == 9 and diff.Y == 18 and diff.Z == 27, "Vector3 subtraction")
local scaled = Vector3.new(1, 2, 3) * 2
assert(scaled.X == 2 and scaled.Y == 4 and scaled.Z == 6, "Vector3 scalar mult")
local divd = Vector3.new(4, 8, 12) / 2
assert(divd.X == 2 and divd.Y == 4 and divd.Z == 6, "Vector3 scalar div")
local negd = -Vector3.new(1, -2, 3)
assert(negd.X == -1 and negd.Y == 2 and negd.Z == -3, "Vector3 unary neg")

-- Vector3 Angle
local a1 = Vector3.new(1, 0, 0)
local a2 = Vector3.new(0, 1, 0)
local angle = a1:Angle(a2)
assert(math.abs(angle - math.rad(90)) < 1e-9, "Vector3 Angle should be 90 degrees")

-- ===== Vector2 properties =====
local v2 = Vector2.new(3, 4)
assert(math.abs(v2.Magnitude - 5) < 1e-9, "Vector2 Magnitude should be 5")
local u2 = v2.Unit
assert(math.abs(u2.Magnitude - 1) < 1e-9, "Vector2 Unit Magnitude should be 1")

-- Vector2 arithmetic
local s2 = Vector2.new(1, 2) + Vector2.new(3, 4)
assert(s2.X == 4 and s2.Y == 6, "Vector2 addition")
local d2 = Vector2.new(10, 20) - Vector2.new(1, 2)
assert(d2.X == 9 and d2.Y == 18, "Vector2 subtraction")
local sc2 = Vector2.new(1, 2) * 3
assert(sc2.X == 3 and sc2.Y == 6, "Vector2 scalar mult")

-- Vector2 Cross (returns scalar in 2D)
local cr2 = Vector2.new(1, 0):Cross(Vector2.new(0, 1))
-- In Roblox, Vector2:Cross returns a scalar (the Z component of the 3D cross)
assert(type(cr2) == "number", "Vector2 Cross should return a number")

-- ===== CFrame properties =====
local cf = CFrame.new(10, 20, 30)
assert(cf.Position.X == 10 and cf.Position.Y == 20 and cf.Position.Z == 30, "CFrame Position")
assert(cf.X == 10 and cf.Y == 20 and cf.Z == 30, "CFrame X/Y/Z")

-- CFrame rotation accessors are unit vectors
local rot = CFrame.Angles(0, 0, 0)
assert(math.abs(rot.LookVector.Magnitude - 1) < 1e-9, "CFrame LookVector should be unit")
assert(math.abs(rot.RightVector.Magnitude - 1) < 1e-9, "CFrame RightVector should be unit")
assert(math.abs(rot.UpVector.Magnitude - 1) < 1e-9, "CFrame UpVector should be unit")

-- PointToObjectSpace / PointToWorldSpace
local origin = CFrame.new(100, 200, 300)
local worldPoint = Vector3.new(101, 202, 303)
local localPoint = origin:PointToObjectSpace(worldPoint)
assert(math.abs(localPoint.X - 1) < 1e-6, "PointToObjectSpace X")
assert(math.abs(localPoint.Y - 2) < 1e-6, "PointToObjectSpace Y")
assert(math.abs(localPoint.Z - 3) < 1e-6, "PointToObjectSpace Z")
local roundtrip = origin:PointToWorldSpace(localPoint)
assert(math.abs(roundtrip.X - 101) < 1e-6, "PointToWorldSpace X roundtrip")
assert(math.abs(roundtrip.Y - 202) < 1e-6, "PointToWorldSpace Y roundtrip")
assert(math.abs(roundtrip.Z - 303) < 1e-6, "PointToWorldSpace Z roundtrip")

-- ===== Color3 properties =====
local c = Color3.fromRGB(128, 64, 32)
assert(c.R == 128/255, "Color3 R")
assert(c.G == 64/255, "Color3 G")
assert(c.B == 32/255, "Color3 B")

-- Color3 fromRGB and fromHSV roundtrip
local hsvColor = Color3.fromHSV(0.5, 1, 1)
assert(typeof(hsvColor) == "Color3", "fromHSV should produce Color3")
local hh, ss, vv = hsvColor:ToHSV()
assert(math.abs(hh - 0.5) < 1e-6, "fromHSV/ToHSV hue roundtrip")
assert(math.abs(ss - 1) < 1e-6, "fromHSV/ToHSV sat roundtrip")
assert(math.abs(vv - 1) < 1e-6, "fromHSV/ToHSV val roundtrip")

-- ===== UDim2 properties =====
local ud = UDim2.new(0.5, 10, 0.3, 20)
assert(ud.X.Scale == 0.5, "UDim2 X.Scale")
assert(ud.X.Offset == 10, "UDim2 X.Offset")
assert(ud.Y.Scale == 0.3, "UDim2 Y.Scale")
assert(ud.Y.Offset == 20, "UDim2 Y.Offset")

-- UDim2 fromScale and fromOffset
local fromS = UDim2.fromScale(1, 1)
assert(fromS.X.Scale == 1 and fromS.X.Offset == 0, "UDim2 fromScale")
local fromO = UDim2.fromOffset(100, 200)
assert(fromO.X.Scale == 0 and fromO.X.Offset == 100, "UDim2 fromOffset")
-- UDim2 __tostring
local s = tostring(ud)
assert(type(s) == "string" and #s > 0, "UDim2 tostring should produce a string")
assert(string.find(s, "10") ~= nil, "UDim2 tostring should contain offset")

-- ===== Random =====
local rng = Random.new(12345)
local n1 = rng:NextInteger(1, 100)
assert(n1 >= 1 and n1 <= 100, "Random NextInteger range")
local n2 = rng:NextNumber()
assert(n2 >= 0 and n2 <= 1, "Random NextNumber range")
local uv = rng:NextUnitVector()
assert(typeof(uv) == "Vector3", "Random NextUnitVector should return Vector3")
assert(math.abs(uv.Magnitude - 1) < 1e-6, "Random NextUnitVector should be unit")
-- Determinism: same seed -> same sequence
local rng2 = Random.new(12345)
assert(rng2:NextInteger(1, 100) == n1, "Random should be deterministic with same seed")

print("properties-contract-ok")
