#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.luau)
trap 'rm -f "$TMP"' EXIT
for i in $(seq 1 201); do
    printf 'local v%s\n' "$i" >> "$TMP"
done
set +e
OUTPUT=$("$VM" "$TMP" 2>&1)
STATUS=$?
set -e
test "$STATUS" -eq 1
grep -Fq 'Out of local registers when trying to allocate v201: exceeded limit 200' <<<"$OUTPUT"
printf 'local-limit-contract-ok\n'
