#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.lua)
trap 'rm -f "$TMP"' EXIT

# In sandbox mode, dangerous globals must be nil.
cat > "$TMP" <<'LUA'
assert(loadstring == nil, "loadstring should be nil in sandbox")
assert(os == nil, "os should be nil in sandbox")
assert(io == nil, "io should be nil in sandbox")
assert(getgenv == nil, "getgenv should be nil in sandbox")
assert(request == nil, "request should be nil in sandbox")
assert(readfile == nil, "readfile should be nil in sandbox")
assert(setclipboard == nil, "setclipboard should be nil in sandbox")
assert(getclipboard == nil, "getclipboard should be nil in sandbox")
assert(getcallstack == nil, "getcallstack should be nil in sandbox")
assert(lumora == nil, "lumora namespace should be nil in sandbox")
print("sandbox-ok")
LUA
OUT=$("$VM" --sandbox "$TMP")
test "$OUT" = "sandbox-ok"

# Without sandbox, loadstring and the host capability namespace are present.
printf 'assert(type(loadstring) == "function")\nassert(type(setclipboard) == "function")\nassert(type(lumora) == "table")\nprint("nosandbox-ok")\n' > "$TMP"
OUT=$("$VM" "$TMP")
test "$OUT" = "nosandbox-ok"
