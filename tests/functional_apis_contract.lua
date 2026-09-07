local CollectionService = game:GetService("CollectionService")
local TweenService = game:GetService("TweenService")
local Debris = game:GetService("Debris")
local StarterGui = game:GetService("StarterGui")

local part = Instance.new("Part")
part.Parent = workspace
local added, removed = 0, 0
CollectionService:GetInstanceAddedSignal("LumoraTag"):Connect(function(value) if value == part then added += 1 end end)
CollectionService:GetInstanceRemovedSignal("LumoraTag"):Connect(function(value) if value == part then removed += 1 end end)
CollectionService:AddTag(part, "LumoraTag")
assert(added == 1 and CollectionService:GetTagged("LumoraTag")[1] == part)
CollectionService:RemoveTag(part, "LumoraTag")
assert(removed == 1 and #CollectionService:GetTagged("LumoraTag") == 0)

local completed = false
local tween = TweenService:Create(part, TweenInfo.new(0.1), {Transparency = 0.75})
tween.Completed:Connect(function() completed = true end)
tween:Play()
assert(part.Transparency == 0.75 and completed and tostring(tween.PlaybackState) ~= "Begin")

local camera = workspace.CurrentCamera
local screen, visible = camera:WorldToViewportPoint(camera.CFrame.Position + camera.CFrame.LookVector * 20)
assert(visible and math.abs(screen.X - camera.ViewportSize.X / 2) < 0.01)
local ray = camera:ViewportPointToRay(camera.ViewportSize.X / 2, camera.ViewportSize.Y / 2)
assert(ray.Direction.Z < 0)

StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.All, false)
assert(StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.All) == false)
StarterGui:SetCore("Test", 42)
assert(StarterGui:GetCore("Test") == 42)

writefile("Lumora/test.asset", "asset-data")
assert(getcustomasset("Lumora/test.asset") == "rbxasset://lumora/Lumora/test.asset")
local ok = pcall(function() request({Url = "https://example.invalid"}) end)
assert(not ok, "request must not report a fake successful response")

Debris:AddItem(part, 0)
task._runScheduler()
assert(part.Parent == nil)
print("functional-apis-ok")
