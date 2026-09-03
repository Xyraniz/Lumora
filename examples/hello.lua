-- hello.lua
-- Minimal example: print a greeting using the Roblox-compatible prelude.
-- Run with: ./bin/lumora examples/hello.lua

local message = Instance.new("Message")
message.Text = "Hello from Lumora!"
print(message.Text)
print("typeof(message):", typeof(message))
print("message:IsA('Instance'):", message:IsA("Instance"))
