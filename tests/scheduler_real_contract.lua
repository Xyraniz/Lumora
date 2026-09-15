local events = {}
local function record(name, value)
    events[#events + 1] = name .. (value and (":" .. tostring(value)) or "")
end

local delayed = false
local delayedElapsed
local handle = task.delay(2.5, function()
    delayed = true
    delayedElapsed = task.wait(1.25)
    record("delayed", delayedElapsed)
end)
task.cancel(task.delay(100, function() error("cancelled task ran") end))
assert(not delayed)
task._step(1)
assert(not delayed, "task.delay ran before its deadline")
task._step(1.5)
assert(delayed, "task.delay did not run at its deadline")
task._runScheduler()
assert(delayedElapsed == 1.25, "task.wait did not return its requested elapsed duration")

local deferred = false
task.defer(function() deferred = true end)
assert(deferred, "top-level task.defer must make one cooperative turn")

local spawned = false
task.spawn(function()
    task.deferSelf()
    spawned = true
end)
assert(not spawned)
task._runScheduler()
assert(spawned, "task.deferSelf did not resume on the next scheduler cycle")

assert(task.status(handle) == "dead", "completed task did not become dead")
assert(#events == 1 and events[1] == "delayed:1.25")
print("scheduler-real-contract-ok")
