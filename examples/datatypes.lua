-- datatypes.lua
-- Demonstrates the emulated Roblox datatypes: Vector3, CFrame, Color3, UDim2.
-- Run with: ./bin/lumora examples/datatypes.lua

local pos = Vector3.new(1, 2, 3)
local dir = Vector3.new(0, 1, 0)
print("pos:", pos)
print("pos magnitude:", pos.Magnitude)
print("pos:Dot(dir):", pos:Dot(dir))
print("pos:Cross(dir):", pos:Cross(dir))

local cf = CFrame.new(10, 20, 30) * CFrame.Angles(0, math.rad(90), 0)
print("cf position:", cf.Position)
print("cf LookVector:", cf.LookVector)

local color = Color3.fromRGB(255, 128, 0)
print("color ToHex:", color:ToHex())
print("color ToHSV:", color:ToHSV())

local ud = UDim2.new(0, 100, 0, 50)
print("udim2:", ud)
