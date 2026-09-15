#!/usr/bin/env bash
set -euo pipefail
VM="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/lumora-analyzer.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/input.luau" <<'LUA'
local _0xabc = string.char(65, 66)
local _0xdef = (2 * 3) + 1
local function _0xfeed() return table.concat({"a", "b"}) .. _0xabc .. tostring(_0xdef) end
request("blocked")
return _0xfeed()
LUA
"$VM" inspect "$TMP/input.luau" --json >"$TMP/report.json"
python3 - "$TMP/report.json" <<'PY'
import json, sys
r=json.load(open(sys.argv[1]))
assert r['summary']['functions'] == 1
assert 'AB' in r['decodedStrings']
assert isinstance(r['decodedBytesHex'], list)
assert isinstance(r['recoveredConstants'], list)
assert len(r['recoveredConstants']) >= 1
assert any(x['category'] == 'network' for x in r['findings'])
assert isinstance(r['hasCycle'], bool)
PY
"$VM" deobfuscate "$TMP/input.luau" --out "$TMP/out" >/dev/null
for f in 00-original.luau 01-strings-decoded.luau 02-constants-folded.luau 03-symbols-renamed.luau 04-reconstructed.luau diff.json report.json; do test -s "$TMP/out/$f"; done
"$VM" report "$TMP/input.luau" >/dev/null
echo analyzer-cli-ok
