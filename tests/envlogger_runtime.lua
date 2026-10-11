assert(type(loadstring) == "function", "envlogger must keep dynamic compilation available")

local dynamic = assert(loadstring([[local players = game:GetService("Players"); return players.Name]]))
assert(dynamic() == "Players", "generated Luau chunk did not run")
assert(task.wait(0.25) == 0.25, "top-level wait should resume in the local scheduler")

local response = request({
    Url = "https://example.invalid/blocked",
    Method = "POST",
    Headers = { ["X-Test"] = "envlogger" },
    Body = "sample",
})
assert(response.Success == false and response.StatusCode == 0, "network stub should report a blocked response")
assert(game:HttpGet("https://example.invalid/loader") == "", "HttpGet should return an empty blocked body")

writefile("envlogger-stage.lua", "return 42")
assert(loadfile("envlogger-stage.lua")() == 42, "memory-only file staging should work")

print("envlogger-runtime-ok")
