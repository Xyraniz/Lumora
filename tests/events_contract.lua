-- Event ordering, signal lifecycle, and destruction tests.
-- Validates that signals fire in connect order, that Disconnect prevents
-- further calls, that Once fires exactly once, and that Destroy cleans up.

-- ===== Connect order =====
local signal = Instance.new("BindableEvent").Event
local order = {}
signal:Connect(function() table.insert(order, "first") end)
signal:Connect(function() table.insert(order, "second") end)
signal:Connect(function() table.insert(order, "third") end)
signal:Fire()
assert(#order == 3, "all three callbacks should fire")
assert(order[1] == "first" and order[2] == "second" and order[3] == "third",
    "callbacks should fire in connect order")

-- ===== Disconnect =====
local signal2 = Instance.new("BindableEvent").Event
local fired = false
local conn = signal2:Connect(function() fired = true end)
assert(conn.Connected == true, "connection should start connected")
conn:Disconnect()
assert(conn.Connected == false, "connection should be disconnected")
signal2:Fire()
assert(fired == false, "disconnected callback should not fire")

-- ===== Once =====
local signal3 = Instance.new("BindableEvent").Event
local count = 0
signal3:Once(function() count = count + 1 end)
signal3:Fire()
signal3:Fire()
signal3:Fire()
assert(count == 1, "Once callback should fire exactly once")

-- ===== DisconnectAll =====
local signal4 = Instance.new("BindableEvent").Event
local c4 = 0
signal4:Connect(function() c4 = c4 + 1 end)
signal4:Connect(function() c4 = c4 + 10 end)
signal4:Fire()
assert(c4 == 11, "both callbacks should fire: " .. c4)
signal4:DisconnectAll()
signal4:Fire()
assert(c4 == 11, "DisconnectAll should prevent further calls")

-- ===== ChildAdded / ChildRemoved ordering =====
local parent = Instance.new("Folder")
local addedNames = {}
local removedNames = {}
parent.ChildAdded:Connect(function(child) table.insert(addedNames, child.Name) end)
parent.ChildRemoved:Connect(function(child) table.insert(removedNames, child.Name) end)

local child1 = Instance.new("Part")
child1.Name = "Alpha"
child1.Parent = parent
local child2 = Instance.new("Part")
child2.Name = "Beta"
child2.Parent = parent

assert(#addedNames == 2, "two children should be added")
assert(addedNames[1] == "Alpha" and addedNames[2] == "Beta", "ChildAdded order")

child1.Parent = nil
assert(#removedNames == 1, "one child removed")
assert(removedNames[1] == "Alpha", "ChildRemoved name")

-- ===== Destroy fires ChildRemoved and removes from parent =====
local parent2 = Instance.new("Folder")
local removedCount = 0
parent2.ChildRemoved:Connect(function() removedCount = removedCount + 1 end)
local d = Instance.new("Part")
d.Name = "Doomed"
d.Parent = parent2
d:Destroy()
assert(removedCount == 1, "Destroy should fire ChildRemoved")
assert(d.Parent == nil, "Destroy should clear Parent")
-- Destroyed instance should not be in children
assert(#parent2:GetChildren() == 0, "Destroy should remove from GetChildren")

-- ===== AttributeChanged =====
local inst = Instance.new("Part")
local attrChanged = false
local attrName = nil
inst.AttributeChanged:Connect(function(name)
    attrChanged = true
    attrName = name
end)
inst:SetAttribute("Health", 100)
assert(attrChanged == true, "AttributeChanged should fire")
assert(attrName == "Health", "AttributeChanged name should match")
assert(inst:GetAttribute("Health") == 100, "GetAttribute should return set value")
inst:SetAttribute("Health", nil)
assert(inst:GetAttribute("Health") == nil, "Setting to nil should clear attribute")

print("events-contract-ok")
