-- json_pipeline.lua
-- Shows how a script can produce structured output for a JSON consumer.
-- Run with: ./bin/lumora --json examples/json_pipeline.lua

-- The script writes to stdout; with --json, Lumora wraps it in the
-- enriched schema (kind, ok, stdout, stderr, message, exitCode,
-- durationMs, timedOut, script).

local results = {
    { name = "check_a", status = "pass" },
    { name = "check_b", status = "pass" },
    { name = "check_c", status = "fail", reason = "expected 42, got 41" },
}

local allPass = true
for _, r in ipairs(results) do
    print(r.name .. ": " .. r.status)
    if r.status ~= "pass" then
        allPass = false
        if r.reason then
            print("  reason: " .. r.reason)
        end
    end
end

if allPass then
    print("ALL CHECKS PASSED")
else
    print("SOME CHECKS FAILED")
    error("validation failed")
end
