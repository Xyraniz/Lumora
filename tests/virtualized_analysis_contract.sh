#!/bin/sh
set -eu
BIN="$1"
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
"$BIN" "$ROOT/tests/path2d_control_point_contract.lua" >"$TMP/path.out"
"$BIN" deobfuscate "$ROOT/tests/virtualized_analysis_fixture.luau" --out "$TMP/a" --deterministic-analysis --trace-globals --trace-indexes --trace-calls --trace-vm --event-limit 100 --instruction-limit 1000 --output-limit 1048576 >/dev/null
"$BIN" deobfuscate "$ROOT/tests/virtualized_analysis_fixture.luau" --out "$TMP/b" --deterministic-analysis --trace-globals --trace-indexes --trace-calls --trace-vm --event-limit 100 --instruction-limit 1000 --output-limit 1048576 >/dev/null
cmp "$TMP/a/trace.jsonl" "$TMP/b/trace.jsonl"
test -s "$TMP/a/vm-ir.json"
test -s "$TMP/a/pseudocode.luau"
grep -q 'blocked-capability' "$TMP/a/trace.jsonl"
grep -q 'GETTABLE' "$TMP/a/vm-ir.json"
"$BIN" deobfuscate "$ROOT/tests/virtualized_analysis_fixture.luau" --out "$TMP/limited" --deterministic-analysis --trace-calls --event-limit 1 --instruction-limit 1000 >/dev/null
test -s "$TMP/limited/trace.jsonl"
printf '%s\n' 'virtualized-analysis: ok'
