local resumed = false
local thread = task.spawn(function()
    task.wait()
    resumed = true
end)
assert(type(thread) == "thread", "task.spawn should return a thread")
task._runScheduler()
assert(resumed, "task.wait should resume on the scheduler")

local explicit = false
local explicitThread = coroutine.create(function()
    explicit = true
end)
task.resume(explicitThread)
assert(explicit, "task.resume should run a suspended thread")

local deferred = false
task.spawn(function()
    task.deferSelf()
    deferred = true
end)
assert(not deferred, "task.deferSelf should yield the current task")
task._runScheduler()
assert(deferred, "task.deferSelf should resume on the next scheduler cycle")

task.spawn(function()
    error("scheduler-contract-error")
end)
local ok, errors = task._runScheduler()
assert(not ok and #errors == 1 and errors[1]:find("scheduler%-contract%-error"),
    "detached task errors should be retained by the scheduler")

print("scheduler-contract-ok")
