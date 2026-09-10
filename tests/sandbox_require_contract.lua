assert(require == nil, "require should be unavailable in sandbox mode")
assert(loadstring == nil, "loadstring should be unavailable in sandbox mode")
assert(readfile == nil, "filesystem compatibility should be unavailable in sandbox mode")
print("sandbox-require-contract-ok")
