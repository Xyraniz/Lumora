-- visual_lab.lua
-- Run with: ./bin/lumora --visual examples/visual_lab.lua
-- The same player/Highlight logic is executed by the Luau runtime and rendered
-- by the native visual laboratory.
local Players = game:GetService("Players")
local localPlayer = Players.LocalPlayer
local simulated = lumora.simulatePlayers({
    {Name = "North", DisplayName = "North", Team = "Blue", Position = Vector3.new(-9, 5, -8)},
    {Name = "East", DisplayName = "East", Team = "Red", Position = Vector3.new(0, 5, -13)},
    {Name = "South", DisplayName = "South", Team = "Blue", Position = Vector3.new(9, 5, -8)},
})

local function attachHighlight(player)
    if player == localPlayer or not player.Character then return end
    local highlight = Instance.new("Highlight")
    highlight.Name = "ESPHighlight"
    highlight.Enabled = true
    highlight.FillColor = player.Team == "Red" and Color3.fromRGB(255, 80, 70) or Color3.fromRGB(70, 150, 255)
    highlight.OutlineColor = Color3.fromRGB(255, 255, 255)
    highlight.Adornee = player.Character
    highlight.Parent = player.Character
end

for _, player in simulated do attachHighlight(player) end
Players.PlayerAdded:Connect(attachHighlight)
local tracer = Drawing.new("Line")
tracer.Visible = true
tracer.From = Vector2.new(640, 360)
tracer.To = Vector2.new(640, 220)
tracer.Color = Color3.fromRGB(255, 220, 70)
local frames = 0
game:GetService("RunService").RenderStepped:Connect(function(delta)
    frames += 1
    tracer.To = Vector2.new(640 + math.sin(frames * delta) * 120, 220)
end)
print("visual lab:", #simulated, "players with script-driven highlights")
