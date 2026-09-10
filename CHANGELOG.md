# Changelog

All notable changes to Lumora are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] — 2026-09-10

### Added
- Native Luau require-by-string support backed by the official `Luau.Require` library. Relative `.lua`/`.luau` modules, `init.lua`/`init.luau` packages, module caching, shared Roblox globals, and useful module chunk names now work in both normal and `--no-roblox` mode.
- `tests/require_contract.lua` plus fixture modules covering relative resolution, package initialization, cache identity, shared prelude globals, and missing-module diagnostics.
- `task.resume` and `task.deferSelf`, based on the cooperative task surface used by Lute.
- Scheduler error collection: detached task failures are retained in the result of `task._runScheduler()` and are reported as process failures when the runtime drains the scheduler.
- `tests/scheduler_contract.lua` covering yield/resume behavior and asynchronous error propagation.
- A headless CMake build path that automatically uses `src/visual_stub.cpp` when SDL2, SDL2_ttf, or SDL2_image is unavailable. The core runtime no longer requires visual dependencies.

### Changed
- Windows drive-qualified paths (`C:/...`) now normalize correctly for chunk names, stack traces, and require resolution.
- `Stats.Network.ServerStatsItem["Data Ping"]` and `Stats.Workspace.Heartbeat` now mirror their child properties, fixing the attached UI fixture when scheduler errors are surfaced.
- `--sandbox` now removes `require` as well as dynamic loading and filesystem compatibility globals, preventing sandboxed scripts from executing arbitrary host modules.
- Version reported by `--version` and `lumora.capabilities()` is now `0.4.0`.

### Fixed
- Asynchronous task errors were silently discarded and could incorrectly produce exit code `0`.
- The visual laboratory remains available when SDL dependencies are installed, while headless/CI builds now fail clearly only when `--visual` is requested without them.

### Fixed
- **CLI parity: `loadstring` and `collectgarbage` are now registered in `--no-roblox` mode** (`registerCLIGlobals` in `src/runtime.cpp`). The official Luau CLI registers both globals on top of the stock base library; Lumora previously lacked them in pure-Luau mode, so any script using them crashed with `attempt to call a nil value`. In Roblox mode the prelude's own Roblox-semantics `loadstring` still takes precedence (registered afterwards), so executor-facing behavior is unchanged.
- **CLI parity: chunk names now use `"@" + normalizePath(path)`** (new `src/paths.cpp`, mirroring `CLI/src/FileUtils.cpp`'s `normalizePath`). Previously the raw path was passed to `luau_load`, so every runtime error, stack trace and `debug.info` location rendered as `[string "script.lua"]` instead of the plain path (`./script.lua:7: ...`), visibly different from official Luau. Relative paths are now normalized with the same `./` prefix and `..`-resolution rules as the reference CLI, making error output byte-identical.
- **CLI parity: scripts now run in a fresh thread via `lua_newthread` + `lua_resume`** exactly like the official CLI's `runFile()`. Previously the main chunk ran via `lua_pcall` on the main state, which made `coroutine.isyieldable()` false at top level and changed error/traceback shape. Top-level yieldability and the `stacktrace:` section of uncaught errors now match the reference runtime.
- **CLI parity: uncaught errors now include the VM's `stacktrace:` section** (`lua_debugtrace`), matching the official CLI's report format. Previously Lumora printed only the message (and the JSON-mode `traceback` field lost function names, which failed `tests/json_schema.sh` after the resume change). The message + `stacktrace:` output for a failing script is now byte-identical to `luau`.
- Removed the now-unused `tracebackHandler` message handler left over from the `lua_pcall` execution path.

### Changed
- **Builtin library tables and the string metatable are now read-only** (`freezeLibraries` in `src/runtime.cpp`), matching both the official CLI (`luaL_sandbox`) and real Roblox, where tampering with `string`, `table`, `math`, `os`, `coroutine`, `debug`, `utf8`, `bit32`, `buffer`, `vector` or `getmetatable("")` errors with `attempt to modify a readonly table`. The globals environment itself stays writable (Roblox parity: scripts assign globals freely; the official CLI's `_G` freeze is a REPL sandbox policy Lumora deliberately does not adopt). `task` and `lumora` remain mutable runtime state.

### Added
- `tests/roblox_parity.lua` — asserts the deliberate divergences from the official CLI that match real Roblox instead: writable globals, frozen library tables/strmeta, working library functions, Roblox globals, task scheduler, loadstring semantics. Registered as the `roblox_parity` CTest target.
- `tests/differential_contract.sh` — differential testing contract: the same probe script must produce byte-identical output under the official `luau` CLI and `lumora --no-roblox`. Skips gracefully (exit 0 with a message) when the reference CLI is not on PATH, so environments without it still pass. Registered as the `differential_contract` CTest target.

### Added
- Native SDL2 visual laboratory with a perspective 3D floor, first-person camera, WASD/mouse input, simulated players, script-driven `Highlight` rendering, labels, ESP lines, and target selection.
- `examples/visual_lab.lua` demonstrates Luau-created players and highlights inside the visual runtime.
- Persistent per-frame Luau signal dispatch for `RunService.Heartbeat`, `RunService.RenderStepped`, and keyboard input through `UserInputService`.
- Software-rendered test walls with line-of-sight occlusion, target selection that ignores blocked players, and rendering of Luau-created `Drawing.Line`/`Drawing.Text` objects.
- Full attached UI library fixture under `examples/attached_library.lua`, including `TextService:GetTextSize`, GUI interaction signals, `Changed`, `Stats.Network.ServerStatsItem.Data Ping`, and native CoreGui traversal.
- Higher-fidelity GUI rendering with SDL_ttf rasterized text, alpha compositing, rounded corners, and `UDim2` scale/offset layout resolution. Roblox asset IDs remain explicit unsupported assets rather than fake textures.

### Added
- Functional headless contracts for `TweenService`, `CollectionService`, camera projection/rays, virtual input, `ContextActionService`, `Debris`, `StarterGui`, and validated virtual assets through `getcustomasset`.
- `tests/functional_apis_contract.lua`, covering state changes, signals, projection, asset validation, and explicit network failure.

### Changed
- HTTP/request and teleport APIs no longer return false successful responses; they fail explicitly because Lumora has no Roblox network/client transport.
- `setclipboard`/`getclipboard` keep deterministic in-memory semantics by default and can opt into a bounded system clipboard backend with `LUMORA_SYSTEM_CLIPBOARD=1`.
- Documentation now distinguishes implemented headless behavior from unavailable external capabilities.

## [0.3.0] — 2026

### Added
- Safe host capability module with process-local `setclipboard`/`getclipboard`, serializable `getcallstack`, `lumora.capabilities()`, and a namespaced `lumora` API.
- In-memory filesystem compatibility with virtual folders, deterministic `listfiles`, `appendfile`, `loadfile`, path validation, and recursive folder deletion.
- Native JSON codec exposed through `HttpService:JSONEncode`, `HttpService:JSONDecode`, `json.encode`, and `json.decode`, including arrays, objects, escapes, cycle detection, and nesting limits.
- `traceback` field in `--json` output and a traceback error handler for nested Luau runtime failures.
- `tests/host_api_contract.lua` and expanded JSON/sandbox contracts.

### Changed
- `GetPropertyChangedSignal` now reuses signals and fires on public property changes, including `Parent` changes.
- Executor hook names remain compatibility stubs and are not expanded into mutation, injection, or client-control features.

### Security
- Clipboard and filesystem compatibility are strictly process-local and memory-backed; no host clipboard or real filesystem access is introduced.

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
- `tests/properties_contract.lua` — regression tests for Vector2/3 properties (Magnitude, Unit, arithmetic), CFrame transforms, Color3.fromRGB/fromHSV, UDim2 properties, and Random determinism.
- `tests/events_contract.lua` — tests for signal connect order, Disconnect, Once, DisconnectAll, ChildAdded/ChildRemoved, Destroy cleanup, and AttributeChanged.
- `tests/negative_contract.lua` — tests for unknown Instance classes, IsA with fake classes, missing attributes/children, WaitForChild with timeout, pcall/xpcall, and Enum access.
- `examples/` directory with four example scripts: `hello.lua`, `datatypes.lua`, `instance_tree.lua`, `json_pipeline.lua`.
- `COMPATIBILITY.md` — detailed per-API compatibility matrix (implemented, partial, not supported).
- Supported platform matrix in README (Ubuntu 22.04/24.04, macOS 13/14, Windows 10/11).
- Release and checksum verification documentation in README.
- Windows CI job (MSVC, windows-latest) in GitHub Actions workflow.
- Cross-platform JSON capture: thread-based `freopen` fallback for non-Unix platforms alongside the existing fork+waitpid path on Unix.

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
- `Random.NextUnitVector` returned a Vector2 instead of a Vector3 — now uses proper spherical distribution.
- `BindableEvent.Event` was nil — now properly exposes the `.Event` signal and `:Fire()` method.
- `WaitForChild` with timeout errored instead of returning nil — now returns nil when a timeout is provided.
- `UDim2` printed as a raw table — now has a `__tostring` metamethod producing `{ScaleX, OffsetX, ScaleY, OffsetY}`.

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
