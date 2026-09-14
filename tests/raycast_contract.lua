local workspace = game:GetService("Workspace")
local wall = Instance.new("Part", workspace)
wall.Name = "RaycastWall"
wall.Position = Vector3.new(0, 0, -10)
wall.Size = Vector3.new(4, 4, 2)
wall.CanCollide = true
local hit = workspace:Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 0, -30))
assert(hit and hit.Instance == wall, "Raycast should hit the nearest BasePart")
assert(math.abs(hit.Distance - 9) < 1e-9, "Raycast distance")
assert(hit.Normal.Z > 0.9, "Raycast normal")
local params = RaycastParams.new()
params.FilterDescendantsInstances = { wall }
assert(workspace:Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 0, -30), params) == nil, "Exclude filter")
wall:Destroy()
print("raycast-contract-ok")
