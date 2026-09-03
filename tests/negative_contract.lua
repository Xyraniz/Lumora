-- Negative tests: invalid arguments, unsupported APIs, and error behavior.
-- These verify that Lumora fails gracefully and predictably, not silently.

-- ===== Instance.new with unknown class =====
local unknown = Instance.new("NonExistentClass")
assert(typeof(unknown) == "Instance", "unknown class should still create an Instance")
assert(unknown.ClassName == "NonExistentClass", "ClassName should match requested name")

-- ===== IsA with non-existent class returns false =====
local part = Instance.new("Part")
assert(part:IsA("TotallyFakeClass") == false, "IsA with fake class should be false")
assert(part:IsA("") == false, "IsA with empty string should be false")

-- ===== IsA with non-string argument =====
-- Roblox would error; we accept either erroring or returning false
local ok, result = pcall(function() return part:IsA(123) end)
-- Either it errors or returns false, both are acceptable
assert(not ok or result == false, "IsA with number should error or return false")

-- ===== GetAttribute on non-existent attribute returns nil =====
local inst = Instance.new("Part")
assert(inst:GetAttribute("DoesNotExist") == nil, "missing attribute should return nil")

-- ===== FindFirstChild on non-existent child returns nil =====
assert(inst:FindFirstChild("Ghost") == nil, "missing child should return nil")
assert(inst:FindFirstChildOfClass("Ghost") == nil, "missing class should return nil")

-- ===== WaitForChild returns child if it exists =====
local child = Instance.new("Part")
child.Name = "Found"
child.Parent = inst
local waited = inst:WaitForChild("Found")
assert(waited == child, "WaitForChild should return the child")
-- WaitForChild on non-existent doesn't block (headless)
local notFound = inst:WaitForChild("NotFound", 0.01)
assert(notFound == nil, "WaitForChild on missing should return nil in headless mode")

-- ===== Vector3 with no args defaults to zero =====
local zero = Vector3.new()
assert(zero.X == 0 and zero.Y == 0 and zero.Z == 0, "Vector3.new() should be zero")

-- ===== pcall catches script errors =====
local success, err = pcall(function()
    error("test-error-message")
end)
assert(success == false, "pcall should catch errors")
assert(string.find(err, "test.error.message", 1, false) ~= nil, "pcall should return error message: " .. tostring(err))

-- ===== pcall returns true on success =====
local ok2, result2 = pcall(function()
    return 42
end)
assert(ok2 == true, "pcall should return true on success")
assert(result2 == 42, "pcall should return the value")

-- ===== xpcall with handler =====
local ok3, err3 = xpcall(function()
    error("xpcall-test")
end, function(e) return "handled: " .. e end)
assert(ok3 == false, "xpcall should return false on error")
assert(string.find(err3, "handled") ~= nil, "xpcall handler should be called: " .. tostring(err3))

-- ===== Enum access =====
assert(typeof(Enum.Material.Plastic) == "EnumItem", "Enum item typeof")
assert(Enum.Material.Plastic.Name == "Plastic", "Enum item name")
assert(Enum.Material.Name == "Material", "Enum type name")

-- ===== task.cancel on a spawned thread =====
local ran = false
local thread = task.spawn(function()
    task.wait(1)
    ran = true
end)
task.cancel(thread)
-- The thread was waiting; cancel should prevent it from running the wait callback
-- (In headless mode, task.wait returns immediately, so this is best-effort)

-- ===== Sandbox removes dangerous globals =====
-- (Tested more thoroughly in sandbox_contract.sh, but verify here too)
-- This test runs without --sandbox, so loadstring should be present
assert(type(loadstring) == "function", "loadstring should exist without sandbox")
assert(type(os) == "table", "os should exist without sandbox")

print("negative-contract-ok")
