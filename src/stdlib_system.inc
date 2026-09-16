static constexpr const char kLumoraSystem[] = R"LUMORA(
--!strict
-- Portable system information surface; every field is filled from a real OS
-- call on the running host (sysctl on macOS, GlobalMemoryStatusEx on Windows,
-- procfs on Linux) through __lumora_host.systemInfo, so it is correct on every
-- platform instead of only where /proc exists.
local M = {}
local info = __lumora_host.systemInfo and __lumora_host.systemInfo() or {}
M.os = info.os or "unknown"
M.arch = info.arch or "unknown"
local function env(name: string): string? return __lumora_host.env(name) end
function M.hostname(): string return env("HOSTNAME") or env("COMPUTERNAME") or "unknown" end
function M.tmpdir(): string return env("TMPDIR") or env("TEMP") or "/tmp" end
function M.threadCount(): number
    local n = tonumber(info.cpuCount) or 1
    return n >= 1 and n or 1
end
function M.cpus(): {{model: string, speed: number, times: {}}}
    -- Report one entry per logical processor; the OS model string is not
    -- portable, so we expose the architecture instead of inventing a model.
    local result = {}
    for _ = 1, M.threadCount() do
        table.insert(result, {model = M.arch, speed = 0, times = {}})
    end
    return result
end
function M.uptime(): number return tonumber(info.uptime) or 0 end
function M.totalMemory(): number return tonumber(info.totalMemory) or 0 end
function M.freeMemory(): number return tonumber(info.freeMemory) or 0 end
return table.freeze(M)
)LUMORA";
