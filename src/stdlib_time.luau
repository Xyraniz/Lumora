--!strict
-- Autonomous duration utilities adapted for Lumora; no external runtime dependency.
local duration = {}
export type Duration = { seconds: number, nanoseconds: number }
local function normalize(seconds: number, nanoseconds: number): Duration
    local extra = math.floor(nanoseconds / 1000000000)
    local nanos = nanoseconds - extra * 1000000000
    local secs = seconds + extra
    if nanos < 0 then secs -= 1; nanos += 1000000000 end
    return { seconds = secs, nanoseconds = nanos }
end
function duration.create(seconds: number, subsecnanos: number): Duration return normalize(seconds, subsecnanos) end
function duration.nanoseconds(n: number): Duration return normalize(0, n) end
function duration.microseconds(n: number): Duration return normalize(0, n * 1000) end
function duration.milliseconds(n: number): Duration return normalize(0, n * 1000000) end
function duration.seconds(n: number): Duration return normalize(n, 0) end
function duration.minutes(n: number): Duration return normalize(n * 60, 0) end
function duration.hours(n: number): Duration return normalize(n * 3600, 0) end
function duration.days(n: number): Duration return normalize(n * 86400, 0) end
function duration.weeks(n: number): Duration return normalize(n * 604800, 0) end
function duration.totalSeconds(d: Duration): number return d.seconds + d.nanoseconds / 1000000000 end
function duration.add(a: Duration, b: Duration): Duration return normalize(a.seconds + b.seconds, a.nanoseconds + b.nanoseconds) end
function duration.sub(a: Duration, b: Duration): Duration return normalize(a.seconds - b.seconds, a.nanoseconds - b.nanoseconds) end
return table.freeze(duration)
