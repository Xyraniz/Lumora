local response = request({
    Url = "http://127.0.0.1:19994/hello",
    Method = "POST",
    Headers = { ["X-Lumora"] = "compatibility" },
    Body = "request-body",
    Timeout = 2,
})
assert(response.Success and response.ok)
assert(response.StatusCode == 201 and response.status == 201)
assert(response.StatusMessage == "Created")
assert(response.Body == "local-request-ok" and response.body == "local-request-ok")
assert(response.Headers["X-Request-Method"] == "POST")
assert(response.Headers["X-Request-Body"] == "request-body")
print("executor-request-contract-ok")
