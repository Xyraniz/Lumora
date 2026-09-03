-- instance_tree.lua
-- Demonstrates the instance hierarchy, parenting, and signals.
-- Run with: ./bin/lumora examples/instance_tree.lua

local model = Instance.new("Model")
model.Name = "MyModel"

local childAdded = false
model.ChildAdded:Connect(function(child)
    childAdded = true
    print("Child added:", child.Name)
end)

local part = Instance.new("Part")
part.Name = "BasePlate"
part.Parent = model

print("Model children count:", #model:GetChildren())
print("ChildAdded fired:", childAdded)

print("FindFirstChild:", model:FindFirstChild("BasePlate").Name)
print("IsA Part -> BasePart:", part:IsA("BasePart"))
print("IsA Model -> Instance:", model:IsA("Instance"))
