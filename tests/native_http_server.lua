local net = require("@lumora/net")
local ok = net.server.listen("127.0.0.1", 19993, function(request)
    assert(request.raw:find("GET /"))
    return "server-ok"
end)
assert(ok == true)
