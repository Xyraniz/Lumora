local mode = arg[1]
local net = require("@lumora/net")
if mode == "http" then
    local result = net.get("http://127.0.0.1:19991/")
    assert(result.status == 200 and result.body == "hello")
else
    local ws = net.websocketConnect("ws://127.0.0.1:19992/")
    ws:send("ping")
    assert(ws:receive() == "pong:ping")
    ws:close()
end
print("network contract ok", mode)
