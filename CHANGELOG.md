# Changelog

All notable changes to Lumora are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] — 2025

### Added
- `--sandbox` flag that removes dangerous globals (`loadstring`, `load`, `os`, `io`, executor hooks, filesystem stubs) and caps the scheduler to 10 cycles.
- Enriched JSON schema with fields `kind`, `ok`, `stdout`, `stderr`, `message`, `exitCode`, `durationMs`, `timedOut`, and `script`. Every error path now produces a single flat JSON object.
- `tests/json_schema.sh` — validates the JSON schema using Python's `json` module as a real parser.
- `tests/sandbox_contract.sh` — verifies that dangerous globals are absent in `--sandbox` mode and present otherwise.
- `tests/datatypes_contract.lua` — regression tests for `typeof` coverage, `Color3` instance methods, `IsA` class hierarchy, `Vector2`/`Vector3` methods, and `CFrame` transforms.
- Security model documentation in README: Lumora is not a security sandbox; untrusted code requires external container isolation.
- GitHub Actions CI workflow (Linux + macOS, GCC + Clang, Release + Debug).
- Modular C++ source layout: `src/main.cpp`, `src/prelude.cpp`, `src/runtime.cpp`, `src/json.cpp`, `src/lumora.h`.

### Changed
- `typeof` now returns the `__type` marker verbatim for all emulated value types (Vector3, Color3, CFrame, UDim2, Ray, etc.) instead of falling through to heuristics.
- `IsA` walks a full class hierarchy table (~80 entries) instead of checking only the immediate class name.
- `CFrame` reimplemented with real matrix math: `CFrame*CFrame` composition, `CFrame*Vector3` transform, `Angles`, `lookAt`, `Inverse`, `PointToObjectSpace`, `PointToWorldSpace`, `VectorToObjectSpace`, `VectorToWorldSpace`, and positional/vector accessors.
- `Color3` gained `ToHex`, `ToHSV`, and `Lerp` as instance methods (PascalCase) with legacy lowercase aliases.
- `Vector3` gained `Dot`, `Cross`, `Lerp`, and `Angle` methods.
- `Vector2` gained `Dot`, `Lerp`, `Cross`, and `Angle` methods.
- Reframed Lumora as a standalone project in README; Fengetheus is now mentioned only as one possible consumer, not as the project's purpose.
- Executor stubs described as a compatibility layer rather than an anti-tamper feature.

### Fixed
- `typeof(Vector3.new(...))` incorrectly returned `"Vector2"` — now returns `"Vector3"`.
- `Color3:ToHex()` was missing — now returns a 6-digit hex string.
- `Part:IsA("BasePart")` returned `false` — now walks the hierarchy and returns `true`.
- Nested JSON on missing script file: the parent now reports a single-level `load-error` object instead of wrapping the child's output.
- `Color3.toHSV` returned zeros — now returns real HSV values.

### Removed
- `readfile` and `isfile` native C closures that accessed the real filesystem. Scripts now only see in-memory stubs from the prelude.

## [0.1.0] — Initial release

### Added
- Headless Luau runtime built on vendored Luau (VM + compiler).
- Roblox-compatible prelude: `game`, `workspace`, `Instance.new`, services, attributes, signals, enumerations, datatypes (`Vector2`, `Vector3`, `UDim`, `UDim2`, `CFrame`, `Color3`, `BrickColor`, `Ray`, `RaycastParams`, `NumberRange`, `NumberSequence`, `ColorSequence`, `Font`, `Rect`, `Path2D`, `TweenInfo`), and a cooperative scheduler.
- CLI with `--no-roblox`, `--json`, `--timeout`, `--help`, and `--version` flags.
- CMake + Ninja build system with CTest integration.
- Smoke tests, CLI contract tests, Roblox API tests, and Luau modern syntax tests.
- PCG32-based deterministic `Random.new`.
