-- Headless contract: a consumer can simulate players and verify an ESP marker.
local Players = game:GetService("Players")
local players = lumora.simulatePlayers({
    { Name = "EnemyAlpha", DisplayName = "Alpha", Position = Vector3.new(10, 5, 0) },
    { Name = "EnemyBeta", DisplayName = "Beta", Position = Vector3.new(-10, 5, 0) },
})
assert(#players == 3)
assert(Players == game:GetService("Players"))

local localPlayer = Players.LocalPlayer
local enemy = players[2]
assert(enemy ~= localPlayer and enemy.Character)
assert(enemy.Character:FindFirstChild("Head"))
assert(enemy.Character:FindFirstChild("HumanoidRootPart"))

local storage = Instance.new("Folder", workspace)
storage.Name = "ESPTestStorage"
local created = 0
for _, player in ipairs(Players:GetPlayers()) do
    if player ~= localPlayer then
        local highlight = Instance.new("Highlight")
        highlight.Adornee = player.Character
        highlight.Enabled = true
        highlight.Parent = storage
        created = created + 1
        print("ESP_OK", player.Name, highlight.Adornee.Name, highlight.Enabled)
    end
end
assert(created == 2)
assert(#storage:GetChildren() == 2)
assert(storage:GetChildren()[1].ClassName == "Highlight")

local added = false
Players.PlayerAdded:Connect(function(player) added = player.Name == "EnemyGamma" end)
lumora.simulatePlayers({ { Name = "EnemyGamma", Position = Vector3.new(0, 5, -8) } })
assert(added)
assert(#Players:GetPlayers() == 4)
print("SIMULATED_PLAYERS_ESP_OK", #Players:GetPlayers())
