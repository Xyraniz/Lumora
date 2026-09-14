-- RunService render-step callbacks must be observable and priority ordered.
local runService = game:GetService("RunService")
local calls = {}

runService:BindToRenderStep("late", 20, function(dt)
    table.insert(calls, "late:" .. tostring(dt))
end)
runService:BindToRenderStep("early", 10, function(dt)
    table.insert(calls, "early:" .. tostring(dt))
end)
runService:BindToRenderStep("same-priority", 10, function()
    table.insert(calls, "same-priority")
end)
runService:_fireRenderStep(0.25)
assert(calls[1] == "early:0.25", "lower render-step priority must run first")
assert(calls[2] == "same-priority", "equal priorities must preserve bind order")
assert(calls[3] == "late:0.25", "higher render-step priority must run last")

runService:UnbindFromRenderStep("early")
calls = {}
runService:_fireRenderStep(0.5)
assert(calls[1] == "same-priority" and calls[2] == "late:0.5", "unbind must remove the callback")

print("runservice-contract-ok")
