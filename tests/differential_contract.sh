#!/usr/bin/env bash
# Differential contract: the exact same script must produce the exact same
# observable output under the official `luau` CLI (ground truth) and under
# `lumora --no-roblox` (pure-Luau mode). Requires the official luau binary on
# PATH; skips with a message when it is not installed so CI environments
# without the reference CLI still pass.
set -euo pipefail
VM="$1"
LUAU="${LUAU_BIN:-luau}"

if ! command -v "$LUAU" >/dev/null 2>&1; then
    echo "differential_contract: official luau CLI not found on PATH — skipping"
    exit 0
fi

TMP=$(mktemp -d /tmp/lumora-diff.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

# Deterministic, seed-free probes with output normalized only for things that
# are invocation artifacts (none today: the paths below are absolute already).
cat > "$TMP/probe.lua" <<'LUA'
-- Observable-behavior probes: values, metamethods, errors, formats.
local function p(label, ...) print(label, ...) end
p("yieldable", coroutine.isyieldable())
p("loadstring.type", type(loadstring))
p("gc.count", type(collectgarbage("count")) == "number")
local ok, err = pcall(function() error("e") end)
p("err.shape", ok, (tostring(err):gsub("^[^:]*", "CHUNK")))
local fn = loadstring("return 1+1")
p("loadstring.run", fn and fn())
local ok2, err2 = pcall(function() string.inject = 1 end)
p("stringlib.readonly", ok2, ok2 and "writable" or "readonly")
p("string.rep", ("ab"):rep(3))
p("math.clamp", math.clamp(9, 0, 3))
p("bit32.band", bit32.band(0xF0, 0x0F))
p("utf8.char", utf8.char(72, 105))
p("format.d", string.format("%.3f", 3.14159))
local co = coroutine.create(function() coroutine.yield("y1") return "r1" end)
local _, a = coroutine.resume(co)
local _, b = coroutine.resume(co)
p("coroutine.roundtrip", a, b)
local t = setmetatable({}, {__add = function(x, y) return "added" end})
p("mm.add", t + 1)
LUA

"$LUAU" "$TMP/probe.lua" > "$TMP/official.out" 2> "$TMP/official.err" || true
"$VM" --no-roblox "$TMP/probe.lua" > "$TMP/lumora.out" 2> "$TMP/lumora.err" || true

if diff "$TMP/official.out" "$TMP/lumora.out" > "$TMP/out.diff" && diff "$TMP/official.err" "$TMP/lumora.err" > "$TMP/err.diff"; then
    echo "differential-ok"
    exit 0
fi

echo "differential mismatch:"
echo "--- stdout diff ---"; cat "$TMP/out.diff"
echo "--- stderr diff ---"; cat "$TMP/err.diff"
exit 1
