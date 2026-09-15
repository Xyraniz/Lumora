<div align="center">
  <img src="assets/lumora-logo.png" alt="Lumora logo" width="180" />
  <h1>Lumora</h1>
  <p><strong>A headless Luau runtime to run, inspect and validate scripts outside Roblox Studio.</strong></p>
  <p>
    <a href="https://github.com/Xyraniz/Lumora/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Xyraniz/Lumora?style=flat-square" alt="MIT License" /></a>
    <a href="https://github.com/Xyraniz/Lumora"><img src="https://img.shields.io/github/languages/top/Xyraniz/Lumora?style=flat-square" alt="Top language" /></a>
    <a href="https://github.com/Xyraniz/Lumora/issues"><img src="https://img.shields.io/github/issues/Xyraniz/Lumora?style=flat-square" alt="Issues" /></a>
  </p>
</div>

Lumora is a self-contained executable built on the official [Luau](https://luau.org) sources. Its goal is to provide a **headless, reproducible and automatable** execution surface for `.lua` and `.luau` scripts that expect common Roblox patterns, without depending on Roblox Studio, a graphical window or a game client.

The project bundles the vendored Luau VM and compiler in `vendor/luau` with an isolated Roblox compatibility prelude. The result is a small, straightforward tool for regression testing, validating generated output, script analysis and CI pipelines. Lumora is an independent project: any tool or workflow that produces `.lua` or `.luau` files can consume it without coupling to a specific generator.

> **Lumora is not Roblox Studio nor a 3D engine.** It emulates a headless surface focused on execution and validation; it does not aim to render experiences, connect to real services or run a full game.

## Why Lumora

Standalone Luau runtimes typically focus on general-purpose programming or providing a full scripting experience. Lumora takes a more specific direction: it prioritizes **practical compatibility with scripts that expect Roblox primitives**, together with a stable CLI for automation. The tool boots with the Roblox-emulating environment enabled by default, allows disabling it to test pure Luau, and offers JSON output to integrate unambiguously with other processes.

| Need | Lumora's answer |
| --- | --- |
| Run Luau without Roblox Studio | Official Luau VM and compiler embedded in a local binary. |
| Validate scripts that use Roblox primitives | Headless prelude with `game`, `workspace`, `Instance`, `Enum`, `task`, types and common services. |
| Integrate execution into CI or pipelines | CLI with no GUI, predictable exit codes and a `--json` mode. |
| Avoid processes that hang | Cooperative timeout in Luau and a process-level safety barrier. |
| Test deterministic behavior | PCG32-based RNG and contract suites for emulated APIs. |

## Main features

Lumora accepts `.lua` and `.luau` files directly, preserves the Luau standard library and supports the compiler’s modern syntax. The Roblox layer includes instance hierarchies, services, attributes, signals, enums, data types and a reduced cooperative scheduler. It also includes `require("./module")` with resolution matching Luau’s official CLI, `init.lua/init.luau` packages and per-execution caching. Headless APIs maintain state, emit events and apply transformations where possible; external capabilities that do not exist in-process fail explicitly instead of returning a silent success.

Normal output preserves the script’s stdout. With `--json`, Lumora returns a structured object containing the execution result, stdout, stderr, error and exit code, allowing consumption from shell scripts, test runners, CI pipelines or tools written in other languages.

## Installation and build

### Requirements

A C++17 compiler, [CMake](https://cmake.org) and [Ninja](https://ninja-build.org) are required. Luau sources are already included in the repository, so the build does not require installing Luau separately or downloading dependencies during compilation. SDL2, SDL2_ttf and SDL2_image are optional: they are only needed to build the `--visual` lab; the headless runtime does not depend on them.

On Debian or Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

### Reproducible build

From the repository root:

```bash
make
```

The recommended executable will be available at `bin/lumora`. The build also creates `bin/luau-vm` as a compatibility alias for older scripts and keeps `bin/run` as a convenient launcher.

```bash
./bin/lumora script.lua argumento1 argumento2
./bin/run script.lua argumento1 argumento2
```

## Supported platforms

Lumora builds and runs on the following platforms. CI validates each combination on every push and pull request.

| Platform | Architecture | Compilers | Status | Notes |
|---|---|---|---|---|
| Ubuntu 22.04 LTS | x86_64 | GCC 11–12, Clang 14 | CI green | Preferred path; JSON uses fork+waitpid |
| Ubuntu 24.04 LTS | x86_64 | GCC 13–14, Clang 18 | CI green | Same as 22.04 |
| macOS 13 (Ventura) | x86_64 | Apple Clang | CI green | JSON uses fork+waitpid |
| macOS 14 (Sonoma) | ARM64 (Apple Silicon) | Apple Clang | CI green | JSON uses fork+waitpid |
| Windows 10/11 | x86_64 | MSVC 2019+ | Manual build | JSON captures via freopen; no process isolation |

`--json` works on all platforms. On Unix (Linux and macOS) `fork`+`waitpid` is used for real process isolation and a hard timeout with `SIGKILL`. On Windows, `stdout`/`stderr` are redirected to temporary files with `freopen` and the script runs on the main thread; Luau’s cooperative interrupt timeout remains available, but there is no hard process-level timeout. For scripts that may enter non-cooperative loops on Windows, wrapping Lumora in a container with CPU limits is recommended.

## CLI usage

```text
lumora [--visual] [--no-roblox] [--json] [--sandbox] [--timeout seconds] script.lua [args...]
```

The Roblox headless mode is enabled by default. `--no-roblox` omits the prelude and runs the file with pure Luau, providing the same globals surface as Luau’s official CLI: `loadstring` and `collectgarbage` are registered (as in `luau`), errors carry the same chunk name and `stacktrace:` section as the official CLI, and the main chunk runs in a coroutine (`coroutine.isyieldable()` is true at top level). The library tables (`string`, `table`, `math`, `os`, `coroutine`, `debug`, `utf8`, `bit32`, `buffer`, `vector`) and the string metatable are frozen, as in real Roblox and the official CLI; writing to them produces `attempt to modify a readonly table`. The globals environment remains writable (parity with Roblox). `--json` captures the execution and writes a single JSON object using the schema documented below. `--sandbox` disables dangerous globals (`loadstring`, `load`, `os`, `io`, executor hooks and filesystem stubs) and limits the scheduler to 10 cycles, useful to constrain the surface available to semi-trusted scripts in a pipeline. `--timeout 5` limits execution to five seconds and prevents an infinite loop from blocking the pipeline. `--help` and `--version` do not execute any script.

`--visual` opens Lumora’s native lab. It runs the script in the same Luau prelude, creates a small world with a floor, a first-person camera, simulated players and renders `Highlight` objects that the script creates. The visual mode uses SDL2 and a software renderer as fallback, so it does not require a GPU or OpenGL. If the binary was built without the optional SDL dependencies, headless mode continues to work and `--visual` exits with an explicit error. It is not combinable with `--json` or `--no-roblox`.

Arguments follow Lua’s usual convention: `arg[0]` contains the script path and `arg[1]` onward contain user-provided arguments. `require` accepts paths relative to the current module, looks for `.luau` before `.lua`, and also accepts directories with `init.luau` or `init.lua`. Results are cached for the duration of execution, as in the official loader.

### Basic commands

Run a script with the Roblox layer:

```bash
./bin/lumora examples/hello.lua
```

Run Luau without Roblox globals:

```bash
./bin/lumora --no-roblox tests/luau_modern.lua
```

Consume a structured response:

```bash
./bin/lumora --json tests/roblox_api.lua
```

Protect a runner against non-terminating loops:

```bash
./bin/lumora --json --timeout 2 generated.luau
```

A successful run yields `exitCode: 0` and `ok: true`. Script errors produce a response with `ok: false`; invalid options or CLI invocation errors use exit code `2`.

### JSON schema

With `--json`, Lumora always writes a single flat JSON object to stdout, without nesting JSON inside `stdout`. The same schema applies to all outcomes (success, load error, compile error, runtime error, timeout and invocation error), so any consumer can read it uniformly.

```json
{
  "kind": "success",
  "ok": true,
  "stdout": "hello-world\n",
  "stderr": "",
  "message": "",
  "traceback": "",
  "exitCode": 0,
  "durationMs": 12,
  "timedOut": false,
  "script": "tests/roblox_api.lua"
}
```

| Field | Type | Description |
| --- | --- | --- |
| `kind` | string | Category of the result: `success`, `load-error`, `compile-error`, `script-error`, `timeout` or `invocation-error`. |
| `ok` | bool | `true` when the script finished with exit code `0`. |
| `stdout` | string | Full stdout of the script, as plain text (never nested JSON). |
| `stderr` | string | Full stderr of the script. |
| `message` | string | Human-readable failure message; empty on success. |
| `traceback` | string | Full diagnostic emitted by Luau, including the call stack; empty when there is no error. |
| `exitCode` | int | Process exit code: `0` (success), `1` (script error/timeout), `2` (load or invocation error). |
| `durationMs` | int | Execution duration in milliseconds, rounded. |
| `timedOut` | bool | `true` if execution was interrupted by `--timeout`. |
| `script` | string | Path of the script passed to Lumora. |

The test `tests/json_schema.sh` validates this schema using Python’s `json` module as a real parser, checking that each error path produces a flat object with all fields, that `stdout` never contains nested JSON and that `timedOut` is distinguished from an ordinary script error.

### Examples

The `examples/` directory contains ready-to-run scripts demonstrating the main capabilities:

```bash
./bin/lumora examples/hello.lua          # Instances and typeof
./bin/lumora examples/datatypes.lua       # Vector3, CFrame, Color3, UDim2
./bin/lumora examples/instance_tree.lua   # Hierarchy, parenting, and signals
./bin/lumora --json examples/json_pipeline.lua  # Structured output for CI
./bin/lumora --visual examples/visual_lab.lua    # 3D world, WASD, ESP and target selection
./bin/lumora --visual examples/attached_library.lua # complete attached UI library
```

### Visual lab

The visual lab is not a disconnected animation: the Luau script creates `Player`, `Character` and `Highlight`, and the renderer queries that same instance tree to draw them. This allows testing ESP logic, target selection and screen-position calculations without opening Roblox. `W`, `A`, `S` and `D` move the camera, the mouse rotates it, `F` toggles visual selection of the nearest target to the center and `Esc` closes the window.

The recommended entry point is:

```bash
./bin/lumora --visual examples/visual_lab.lua
```

The attached UI library is also integrated as `examples/attached_library.lua`. It runs fully in headless and visual mode: it creates its `CoreGui`, frames, labels, buttons, sliders, colorpickers, input connections, tweening, notifications and settings. The `attached_library_load` contract runs in CTest to prevent future changes from breaking its loading.

The visual renderer now uses rasterized fonts via SDL_ttf, alpha transparency, rounded rectangles, deterministic text metrics and `UDim2` scaling over a fixed 1280×720 viewport. This substantially improves UI legibility and proportion, but does not promise pixel-perfect parity with Roblox: that would require the same rasterizer, exact fonts, image atlases and Roblox’s proprietary compositor. `rbxassetid://` images are not downloaded nor presented as if authentic unless a corresponding local asset exists.

The scene includes a floor with perspective grid, test walls, per-player colored lighting, labels, ESP boxes, center-screen lines and target selection. The renderer issues `RunService.Heartbeat` and `RunService.RenderStepped` each frame, dispatches input to `UserInputService`, draws `Drawing.Line` objects and respects occlusion against walls. Pressing `F` orients the camera toward the visible player nearest the center; this lets you test selection logic without relying on an external service. Physics are deliberately limited to camera movement, occlusion and deterministic positioning; it does not aim to reproduce Roblox’s full physics engine.

## Emulated Roblox surface

The following table summarizes the API covered by the current prelude. Compatibility is deliberately **headless**: local operations have observable logic and operations without a local equivalent produce an explicit error instead of pretending a Roblox response.

| Area | Available surface |
| --- | --- |
| Instance tree | `game`, `workspace`, `Instance.new`, `GetService`, `GetChildren`, `FindFirstChild`, `FindFirstChildOfClass`, `WaitForChild`, `GetFullName`, `Destroy`, `IsA`. |
| Hierarchy | `Parent`, reparenting without duplication, `ChildAdded`, `ChildRemoved` and recursive destruction. |
| Events and signals | `Connect`, `Once`, `Disconnect`, `Connected`, `DisconnectAll`, `Fire`, `AttributeChanged`. |
| Attributes | `GetAttribute` and `SetAttribute`. |
| Enums | `Enum.X.Y`, `Name`, `EnumType`, `FromName`, `FromValue` and `Value`. |
| Data types | `typeof`, `Vector2`, `Vector3`, `UDim`, `UDim2`, `CFrame`, `Color3`, `BrickColor`, `Ray`, `RaycastParams`, `NumberRange`, `NumberSequence`, `ColorSequence`, `Font`, `Rect`, `Path2D` and `TweenInfo`. |
| Scheduling | Cooperative `task.spawn`, `task.defer`, `task.delay`, `task.cancel`, `task.resume`, `task.status`, `task.deferSelf`, and `task.wait`, plus the usual global aliases. Delays use deterministic virtual time, waits resume only at their deadlines, cancellation closes queued coroutines, and detached task errors are preserved and reported. `RunService:BindToRenderStep` callbacks run in deterministic priority order and can be removed with `UnbindFromRenderStep`. |
| Environment functions | `iscclosure`, `islclosure`, `newcclosure`, `clonefunction`, `getfenv`, `setfenv`, `getgenv`, `getrenv` and an executor compatibility layer (safe stubs). |
| Safe host capabilities | `setclipboard`/`getclipboard` in-memory by default; system clipboard opt-in via `LUMORA_SYSTEM_CLIPBOARD=1`, plus `getcallstack` and `lumora.capabilities()`. They do not access Roblox. |
| Test filesystem | `writefile`, `readfile`, `appendfile`, `isfile`, `isfolder`, `makefolder`, `delfile`, `delfolder`, `listfiles` and `loadfile` over an ephemeral in-memory filesystem. The native `@lumora/fs` module adds typed metadata, directory reads, recursive copy/move/remove, and file/directory predicates without host filesystem access. |
| JSON | `HttpService:JSONEncode`, `HttpService:JSONDecode`, `json.encode` and `json.decode`, with deterministic objects and cycle/depth errors. |
| Randomness | `Random.new(seed)`, `NextInteger`, `NextNumber`, `NextUnitVector` and `Clone`, with deterministic PCG32 state. |
| Luau library | Standard library, `bit32`, `string.pack/unpack`, `buffer`, `utf8` and the compiler’s modern syntax. |

The implementation is maintained in an isolated prelude at `src/prelude.cpp` (with native C closures) and execution utilities in `src/runtime.cpp`. This design allows expanding the compatibility surface without modifying the vendored VM or coupling it to a graphical client. Exact parity with a specific Roblox version should be validated using golden vectors for that version; Lumora prioritizes the observable compatibility required by its tests and pipelines.

### Analysis and observability APIs

Lumora includes dependency-free analysis primitives designed for reproducible inspection of generated or unknown Luau. `require("@lumora/regex")` provides ECMAScript regular-expression matching, capture groups, replacement and splitting. `require("@lumora/analysis")` provides `scan(source)` for capability-sensitive findings and `graph(source)` for function, call and module edges. Passing `"@file:path/to/module.luau"` makes `graph` resolve relative `require` calls recursively, build a global module graph and run DFS cycle detection. The result includes `modules`, `moduleEdges`, `moduleCount`, `edgeCount`, `hasCycle` and closed paths in `cycles`. The results are ordinary Luau tables and can be serialized through the existing JSON helpers.

The virtual filesystem supports `lumora.fs.snapshot()` and `lumora.fs.diff(before, after)`. Snapshots contain deterministic file contents and folders; diffs report sorted `added`, `removed` and `changed` paths, which makes every transformation stage auditable. The cooperative scheduler exposes `task.trace(true|false)`, `task.traceEvents(clear)` and `task.clearTrace()`. Events include task creation, delay, yield, completion, cancellation and errors, together with virtual time, cycle and wake deadline.

The `lumora.capabilities()` result explicitly reports `analysis`, the dynamic-code policy, and process policy. These facilities do not grant network access or Roblox-client access: unavailable external behavior remains blocked and observable. They are intended to provide structured evidence to a human or another model rather than claim that a reconstructed script is identical to its original source.

The local CLI exposes the complete static pipeline without network services:

```bash
lumora inspect suspicious.luau --json
lumora report suspicious.luau --json
lumora deobfuscate suspicious.luau --out analysis-output
```

The deobfuscation command writes `00-original.luau`, `01-strings-decoded.luau`, `02-constants-folded.luau`, `03-symbols-renamed.luau`, `04-reconstructed.luau`, `diff.json` and `report.json`. Transformations are conservative: dynamic code is reported, not executed automatically; external network and host filesystem access remain disabled by the runtime policy. The report records recovered strings, capability findings, function calls, recursively resolved local modules and circular dependencies.

### Player simulation for ESP tests

To validate visual logic reproducibly, the prelude exposes `lumora.simulatePlayers(specs)`. It creates synthetic players and characters with `Head` and `HumanoidRootPart`, and fires `Players.PlayerAdded`; `lumora.resetSimulatedPlayers()` clears that state. `Highlight`, `BillboardGui` and `Drawing` are headless-observable objects: they allow asserting that an ESP is created, targets the correct character and is enabled, but do not draw or interact with a real client. The executable contract lives in `tests/simulated_players_esp.lua`.

## Repository architecture

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | CLI parsing, orchestration of execution (fork/exec, process-level timeout) and assembly of the JSON result. |
| `src/prelude.cpp` | Embedded headless Roblox prelude (Lua) and native C closures (`loadstring`, `type`, `typeof`, `iscclosure`, etc.) registering globals. |
| `src/runtime.cpp` | Execution utilities: file reading, argument passing, cooperative timeout, `--sandbox` mode, freezing libraries, registering official CLI globals (`loadstring`/`collectgarbage`), error diagnostics with `stacktrace:` and `runScript`. |
| `src/paths.cpp` | Path normalization identical to `normalizePath` from the official CLI (`CLI/src/FileUtils.cpp`), so chunk names and error locations match `luau` byte-for-byte. |
| `src/require.cpp` | Module loader based on `Luau.Require`, with relative resolution, `init.*` packages, cache and isolated coroutine execution. |
| `src/json.cpp` | String escaping, `HttpService`/`json` JSON codec and type/structure validation. |
| `src/host_api.cpp` | Local host capabilities: clipboard, stack inspection and capabilities metadata. |
| `src/visual.cpp` / `src/visual_stub.cpp` | SDL2 lab when dependencies are available, or explicit headless diagnostics when not. |
| `src/lumora.h` | Shared declarations across C++ modules. |
| `vendor/luau` | Vendored official Luau sources, including VM, compiler and common library. |
| `tests/` | Smoke tests, CLI contracts, modern syntax, Roblox API, hierarchy, signals and scheduling. |
| `CMakeLists.txt` | C++17 target, Luau integration, executable generation and CTest registration. |
| `Makefile` | Reproducible shortcuts to build, test and clean. |
| `bin/run` | Launcher that resolves the repository root and delegates to `bin/lumora`. |
| `assets/lumora-logo.png` | Project’s primary visual mark for README, docs and distribution. |

The execution flow is intentionally simple:

```text
script.lua / script.luau
        │
        ▼
Lumora CLI ──► Luau compiler ──► VM Luau
        │                              │
        │                              ├─ Prelude Roblox headless
        │                              ├─ Cooperative scheduler
        │                              └─ Timeout / interrupt
        ▼
stdout, stderr, exit code or JSON result
```

## Tests

The suite runs with:

```bash
make test
```

The direct equivalent is:

```bash
ctest --test-dir build --output-on-failure
```

Tests cover argument handling and stdout, `--json`, timeouts, stack traces, modern syntax, standard library, `require` of modules and packages, hierarchy and instance reparenting, child added/removed events, attributes, enums, fidelity of data types (Vector2/3, CFrame, Color3, UDim2), class inheritance with `IsA`, recursive destruction, `:` calls, property signals, scheduling, resume and task errors, basic cancellation, in-memory host capabilities, JSON codec, `--sandbox` mode and JSON schema validation with a real parser.

Additionally, two new contracts cover parity with Luau’s official CLI:

- `tests/roblox_parity.lua` checks the deliberate divergences that mimic real Roblox: writable globals, frozen library tables (writing to `string`/`table`/`math`/... or to `getmetatable("")` yields `attempt to modify a readonly table`), intact library functions, Roblox globals and an operative scheduler.
- `tests/differential_contract.sh` compares the byte-for-byte output of the same script under the official `luau` CLI (reference) and `lumora --no-roblox`, including error formatting, chunk names, `coroutine.isyieldable` and the read-only surface. It skips with a warning if `luau` is not in PATH.

## Integration with other tools

Lumora is an independent project and does not belong to any particular code generator. Any tool that produces `.lua` or `.luau` files can use Lumora as an execution stage: it accepts the file, prepares a compatible environment and produces a reproducible result for tests, output validation and automation. For example, a code generator like [Fengetheus](https://github.com/Xyraniz/Fengetheus) can delegate execution to Lumora, but integration is optional and Lumora works equally well with hand-written scripts or those generated by any other tool.

## Security model

Lumora runs Luau code with access to the full standard library and, by default, additional globals such as `loadstring` and the executor compatibility layer. **Lumora is not a security sandbox.** It is designed to run scripts that are under reasonable control or trust within a CI pipeline or a local validation flow. To execute untrusted or unknown-origin code, use an external container (Docker, Linux namespaces, VM, etc.) that isolates the filesystem, network and processes.

`setclipboard`/`getclipboard` functions use an in-process string by default. If `LUMORA_SYSTEM_CLIPBOARD=1` is set, they also attempt to use `wl-copy`/`wl-paste`, `xclip` or `xsel`, with a timeout and fallback to memory. `writefile` and related functions operate over an ephemeral in-memory filesystem; none of these APIs read or modify the real filesystem. `getcallstack` and `lumora.capabilities()` only expose local diagnostic metadata. Executor hook functions remain compatibility stubs and do not alter functions or metatables; network calls and teleport fail explicitly because there is no Roblox transport.

### `--sandbox` mode

The `--sandbox` flag reduces the surface available to the script, useful when processing semi-trusted scripts in a pipeline and you want to fail fast on attempts to access dangerous primitives. Specifically, `--sandbox`:

- Removes `loadstring` and `load` (no arbitrary runtime compilation).
- Removes `require` (modules on the filesystem won’t execute from the reduced environment).
- Removes the `os` and `io` libraries (no filesystem or process environment access).
- Removes executor hooks and the prelude filesystem stubs.
- Limits the cooperative scheduler to 10 cycles, constraining the work a script can enqueue.

`--sandbox` does not replace OS-level isolation: it is a surface-reduction layer within the process, not a full security barrier. The test `tests/sandbox_contract.sh` verifies that dangerous globals are absent in `--sandbox` mode and present in normal mode.

## Scope and non-goals

Lumora is intended to run and validate scripts in a local environment, not to replace Roblox. It does not render UI, simulate the full physics engine, open a window, provide real Roblox connectivity or guarantee that a complete experience will run outside of Roblox. APIs that require external state are emulated safely and should be treated as compatibility contracts, not access to production services.

## Versioning and changelog

Lumora follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Notable changes for each version are documented in [CHANGELOG.md](CHANGELOG.md). The detailed compatibility matrix by API (implemented, partial, unsupported) is in [COMPATIBILITY.md](COMPATIBILITY.md).

## Releases and checksums

Stable releases are published as source tarballs tagged in Git (`git tag v0.2.0`) and as prebuilt binaries for Linux x86_64 and macOS universal. Each release includes a `SHA256SUMS.txt` file with checksums for all artifacts.

To verify a downloaded binary:

```bash
sha256sum -c SHA256SUMS.txt --ignore-missing
```

To build a reproducible binary locally and compare:

```bash
git checkout v0.2.0
make
sha256sum bin/lumora
```

The build is deterministic for the same combination of compiler, flags and vendored Luau version, so the resulting hash should match the one published for the corresponding release. Checksums for each version are kept on the repository’s [Releases](https://github.com/Xyraniz/Lumora/releases) page.

## Updating the vendored Luau

Luau sources are included in `vendor/luau` to guarantee reproducible builds without external downloads. To update to a newer version:

1. Replace the contents of `vendor/luau` with the new version from [luau-lang/luau](https://github.com/luau-lang/luau).
2. Verify that headers `lua.h`, `lualib.h` and `Luau/Compiler.h` remain available at the expected paths (`vendor/luau/VM/include` and `vendor/luau/Compiler/include`).
3. Run `make` and `make test` to confirm the build and tests pass.
4. Update the release note in `CHANGELOG.md` if there are behavioral changes.

## Contributing

Contributions should include an explanation of the expected behavior, a regression test when possible and a clear description of any difference with Luau or Roblox. For changes to the prelude, it is advisable to add a small deterministic case to `tests/` before expanding the surface. Pull requests that change the CLI should preserve the exit codes and JSON format documented in this file.

## License

Lumora is distributed under the [MIT license](LICENSE). Vendored Luau sources retain their original notices and conditions within `vendor/luau`.

## References

[1]: https://luau.org "Luau"
[2]: https://github.com/runtime-org/runtime "upstream runtime — standalone Luau runtime"
[3]: https://github.com/luau-lang/lute "Lute — standalone Luau runtime for general-purpose programming"

## Standalone compatibility with upstream runtime runners

Lumora can execute runners generated for upstream runtime’s usual contract without installing upstream runtime or downloading modules at runtime. `lumora run script.luau` and the local launcher `bin/runtime script.luau` are equivalent; both load `@lumora/fs`, `@lumora/luau`, `@lumora/stdio` and `@lumora/process` modules from the binary itself. Lumora also provides its independent `@lumora/fs` module, adapted from the useful shape of upstream runtime’s filesystem API but implemented entirely over Lumora’s virtual memory filesystem. The embedded `@lumora/fs` implementation now exposes real file/directory predicates plus recursive copy and rename, while `@lumora/process.exec` executes an explicitly supplied local command and returns captured output, exit code and success state. No upstream runtime source, repository reference or runtime dependency is used. Network and Roblox-client operations remain explicit failures rather than fabricated responses.
