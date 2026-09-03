-- Regression tests for the contracts Manus reported as broken.
-- typeof must recognize every emulated datatype, Color3 must expose ToHex
-- and ToHSV as instance methods, and IsA must walk the class hierarchy.

-- typeof coverage for all value types
assert(typeof(Vector3.new(1, 2, 3)) == "Vector3")
assert(typeof(Vector2.new(1, 2)) == "Vector2")
assert(typeof(Color3.new(0.1, 0.2, 0.3)) == "Color3")
assert(typeof(CFrame.new(1, 2, 3)) == "CFrame")
assert(typeof(UDim.new(1, 2)) == "UDim")
assert(typeof(UDim2.new(1, 2, 3, 4)) == "UDim2")
assert(typeof(Ray.new(Vector3.new(), Vector3.new())) == "Ray")
assert(typeof(RaycastParams.new()) == "RaycastParams")
assert(typeof(NumberRange.new(0, 1)) == "NumberRange")
assert(typeof(NumberSequence.new(0)) == "NumberSequence")
assert(typeof(ColorSequence.new(Color3.new())) == "ColorSequence")
assert(typeof(TweenInfo.new()) == "TweenInfo")
assert(typeof(BrickColor.new()) == "BrickColor")
assert(typeof(Rect.new()) == "Rect")
assert(typeof(Path2D.new()) == "Path2D")
assert(typeof(Font.new()) == "Font")

-- type() reports "userdata" for all Roblox datatypes (matches real Roblox)
assert(type(Vector3.new()) == "userdata")
assert(type(Color3.new()) == "userdata")
assert(type(CFrame.new()) == "userdata")
assert(type(game) == "userdata")

-- Color3 instance methods
local red = Color3.fromRGB(255, 0, 0)
assert(red:ToHex() == "ff0000", "Color3:ToHex failed")
assert(red:toHex() == "ff0000", "Color3:toHex legacy alias failed")
local h, s, v = red:ToHSV()
assert(h == 0 and s == 1 and v == 1, "Color3:ToHSV returned " .. h .. "," .. s .. "," .. v)
local lerped = red:Lerp(Color3.fromRGB(0, 255, 0), 0.5)
assert(math.abs(lerped.R - 0.5) < 1e-9, "Color3:Lerp R failed")
assert(math.abs(lerped.G - 0.5) < 1e-9, "Color3:Lerp G failed")
assert(lerped.B == 0, "Color3:Lerp B failed")
-- Static Color3.toHSV must also work (not return zeros)
local h2, s2, v2 = Color3.toHSV(red)
assert(h2 == 0 and s2 == 1 and v2 == 1, "Color3.toHSV static failed")
-- Green has hue 1/3
local green = Color3.fromRGB(0, 255, 0)
local hg, sg, vg = green:ToHSV()
assert(math.abs(hg - 1/3) < 1e-6, "green hue should be 1/3, got " .. hg)

-- IsA class hierarchy
assert(Instance.new("Part"):IsA("BasePart"), "Part should be BasePart")
assert(Instance.new("Part"):IsA("PVInstance"), "Part should be PVInstance")
assert(Instance.new("Part"):IsA("Instance"), "Part should be Instance")
assert(Instance.new("MeshPart"):IsA("BasePart"), "MeshPart should be BasePart")
assert(Instance.new("Model"):IsA("Instance"), "Model should be Instance")
assert(Instance.new("TextLabel"):IsA("GuiObject"), "TextLabel should be GuiObject")
assert(Instance.new("TextLabel"):IsA("Instance"), "TextLabel should be Instance")
assert(not Instance.new("Part"):IsA("Model"), "Part is not a Model")
assert(Instance.new("Folder"):IsA("Instance"), "Folder should be Instance")

-- Vector3 methods
local v3a = Vector3.new(1, 2, 3)
local v3b = Vector3.new(4, 5, 6)
assert(v3a:Dot(v3b) == 32, "Vector3:Dot failed")
local cross = v3a:Cross(v3b)
assert(cross.X == -3 and cross.Y == 6 and cross.Z == -3, "Vector3:Cross failed")
local lerp = v3a:Lerp(v3b, 0.5)
assert(lerp.X == 2.5 and lerp.Y == 3.5 and lerp.Z == 4.5, "Vector3:Lerp failed")

-- Vector2 methods
local v2a = Vector2.new(1, 2)
local v2b = Vector2.new(3, 4)
assert(v2a:Dot(v2b) == 11, "Vector2:Dot failed")
local lerp2 = v2a:Lerp(v2b, 0.5)
assert(lerp2.X == 2 and lerp2.Y == 3, "Vector2:Lerp failed")

-- CFrame real transforms
local rot = CFrame.Angles(0, math.rad(90), 0)
local transformed = rot * Vector3.new(1, 0, 0)
assert(math.abs(transformed.X) < 1e-9, "CFrame rotate X failed")
assert(math.abs(transformed.Y) < 1e-9, "CFrame rotate Y failed")
assert(math.abs(transformed.Z - (-1)) < 1e-9, "CFrame rotate Z failed")
-- CFrame * CFrame composition
local cf1 = CFrame.new(1, 0, 0)
local cf2 = CFrame.new(0, 1, 0)
local composed = cf1 * cf2
assert(math.abs(composed.X - 1) < 1e-9, "CFrame*CF X failed")
assert(math.abs(composed.Y - 1) < 1e-9, "CFrame*CF Y failed")
-- lookAt LookVector points toward target
local look = CFrame.lookAt(Vector3.new(0, 0, 0), Vector3.new(0, 0, -10))
assert(math.abs(look.LookVector.X) < 1e-9, "lookAt LookVector X")
assert(math.abs(look.LookVector.Z - (-1)) < 1e-9, "lookAt LookVector Z")
-- Inverse: CFrame * Inverse == identity (position)
local inv = cf1:Inverse()
local roundtrip = cf1 * inv
assert(math.abs(roundtrip.X) < 1e-9, "CFrame Inverse X failed")

print("datatypes-contract-ok")
