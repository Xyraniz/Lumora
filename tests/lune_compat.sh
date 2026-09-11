#!/usr/bin/env bash
set -euo pipefail
VM="$1"
ROOT="$(cd -- "$(dirname -- "$VM")/.." && pwd)"
if [ ! -f "$ROOT/tests/lune_compat_contract.lua" ]; then ROOT="$(cd -- "$ROOT/.." && pwd)"; fi
OUT=$("$VM" run "$ROOT/tests/lune_compat_contract.lua")
test "$OUT" = "lune-compat-ok"
OUT=$("$ROOT/bin/lune" "$ROOT/tests/lune_compat_contract.lua")
test "$OUT" = "lune-compat-ok"
