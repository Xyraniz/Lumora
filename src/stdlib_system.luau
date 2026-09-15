--!strict
-- Portable system information surface; intentionally reports only Lumora host data.
local M = {}
M.os = if jit and jit.os then jit.os else "unknown"
M.arch = if jit and jit.arch then jit.arch else "unknown"
function M.hostname(): string return os.getenv("HOSTNAME") or os.getenv("COMPUTERNAME") or "unknown" end
function M.tmpdir(): string return os.getenv("TMPDIR") or "/tmp" end
function M.threadCount(): number return 1 end
function M.cpus(): {{model: string, speed: number, times: {}}} return {{model = "unknown", speed = 0, times = {}}} end
function M.uptime(): number return os.clock() end
function M.freeMemory(): number return 0 end
function M.totalMemory(): number return 0 end
return table.freeze(M)
