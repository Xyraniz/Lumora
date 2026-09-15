--!strict
-- Portable system information surface; intentionally reports only Lumora host data.
local M = {}
M.os = "linux"
M.arch = "unknown"
local function env(name: string): string? return __lumora_host.env(name) end
function M.hostname(): string return env("HOSTNAME") or env("COMPUTERNAME") or "unknown" end
function M.tmpdir(): string return env("TMPDIR") or "/tmp" end
local function proc(path: string): string?
    local ok, value = pcall(__lumora_host.readFile, path)
    return ok and value or nil
end
local cpuinfo = proc("/proc/cpuinfo") or ""
local meminfo = proc("/proc/meminfo") or ""
local function countLines(text: string, prefix: string): number
    local n = 0
    for line in text:gmatch("[^\n]+") do if line:sub(1, #prefix) == prefix then n += 1 end end
    return n
end
function M.threadCount(): number
    local n = countLines(cpuinfo, "processor")
    return n > 0 and n or 1
end
function M.cpus(): {{model: string, speed: number, times: {}}}
    local result = {}
    for model in cpuinfo:gmatch("model name%s*:%s*([^\n]+)") do table.insert(result, {model = model, speed = 0, times = {}}) end
    if #result == 0 then table.insert(result, {model = "unknown", speed = 0, times = {}}) end
    return result
end
function M.uptime(): number
    local value = proc("/proc/uptime")
    return tonumber(value and value:match("^[%d%.]+")) or os.clock()
end
local function memoryValue(name: string): number
    local value = tonumber(meminfo:match(name .. ":%s*(%d+)%s*kB"))
    return (value or 0) * 1024
end
function M.freeMemory(): number return memoryValue("MemFree") end
function M.totalMemory(): number return memoryValue("MemTotal") end
return table.freeze(M)
