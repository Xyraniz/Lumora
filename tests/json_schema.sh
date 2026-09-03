#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.lua)
trap 'rm -f "$TMP"' EXIT

# Validate that every JSON path produces a SINGLE flat JSON object (no nested
# JSON inside stdout) and that the enriched schema fields are present. We use
# Python's json module as a real parser to guarantee schema compliance.

python3 - "$VM" "$TMP" <<'PY'
import json, subprocess, sys, os, tempfile

vm = sys.argv[1]
tmp = sys.argv[2]

def run(args, content=None):
    if content is not None:
        with open(tmp, "w") as f:
            f.write(content)
    r = subprocess.run([vm] + args, capture_output=True, text=True)
    return r

required_fields = {"kind", "ok", "stdout", "stderr", "message", "exitCode", "durationMs", "timedOut", "script"}

def check(obj, expected_kind=None):
    missing = required_fields - set(obj)
    assert not missing, f"missing fields: {missing}"
    # stdout must be a plain string, never nested JSON
    assert isinstance(obj["stdout"], str), "stdout must be a string"
    if obj["stdout"].lstrip().startswith("{"):
        raise AssertionError("stdout contains nested JSON: " + repr(obj["stdout"]))

# 1. Missing file -> single-level load-error
r = run(["--json", "/nonexistent/lumora-probe.lua"])
obj = json.loads(r.stdout)
check(obj, "load-error")
assert obj["kind"] == "load-error", obj["kind"]
assert obj["ok"] is False
assert obj["exitCode"] == 2
assert obj["timedOut"] is False
assert obj["script"] == "/nonexistent/lumora-probe.lua"
assert "cannot open script" in obj["message"]

# 2. Success
r = run(["--json", tmp], content='print("hello-world")\n')
obj = json.loads(r.stdout)
check(obj, "success")
assert obj["kind"] == "success", obj["kind"]
assert obj["ok"] is True
assert obj["exitCode"] == 0
assert "hello-world" in obj["stdout"]
assert obj["message"] == ""
assert obj["timedOut"] is False

# 3. Runtime error
r = run(["--json", tmp], content='error("boom-test")\n')
obj = json.loads(r.stdout)
check(obj, "script-error")
assert obj["kind"] == "script-error", obj["kind"]
assert obj["ok"] is False
assert obj["exitCode"] == 1
assert "boom-test" in obj["message"]
assert obj["timedOut"] is False

# 4. Timeout
r = run(["--json", "--timeout", "0.1", tmp], content='while true do end\n')
obj = json.loads(r.stdout)
check(obj)
assert obj["ok"] is False
assert obj["timedOut"] is True, f"expected timedOut=True, got {obj}"
assert obj["exitCode"] == 1

# 5. durationMs is a non-negative integer
assert isinstance(obj["durationMs"], int) and obj["durationMs"] >= 0

print("json-schema-ok")
PY
