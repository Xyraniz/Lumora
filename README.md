<div align="center">
<img src="assets/lumora-logo.png" alt="Lumora logo" width="180" />
  <h1>Lumora</h1>
  <p><strong>A standalone Luau runtime to run, inspect, and validate scripts outside Roblox Studio.</strong></p>
  <p>
    <a href="https://github.com/Xyraniz/Lumora/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Xyraniz/Lumora?style=flat-square" alt="MIT License" /></a>
    <a href="https://github.com/Xyraniz/Lumora"><img src="https://img.shields.io/github/languages/top/Xyraniz/Lumora?style=flat-square" alt="Top language" /></a>
    <a href="https://github.com/Xyraniz/Lumora/issues"><img src="https://img.shields.io/github/issues/Xyraniz/Lumora?style=flat-square" alt="Issues" /></a>
  </p>
</div>

Lumora is a standalone C++ runtime for running Luau scripts outside the upstream Roblox runtime. It embeds Luau, adds a Roblox-shaped compatibility layer, and ships several host, analysis, and native-module surfaces in the executable itself.

The project is useful when a script needs a local Luau runner, deterministic inspection, a controlled sandbox, or a host API that resembles the parts of Roblox commonly used by scripts. It is not a Roblox client and does not pretend to provide engine state that it cannot reproduce.

## What it provides

Lumora has two related entry points. The normal runner executes a `.lua` or `.luau` script. The analyzer commands inspect a script without treating it as an ordinary run.

The runtime includes the following surfaces:

- Luau execution through the embedded Luau VM.

- Roblox-shaped globals such as `game`, `Instance`, `Enum`, `Vector2`, `Vector3`, `CFrame`, `Color3`, `UDim`, `UDim2`, `Ray`, `Region3`, `Random`, `task`, and `RunService`.

- Relative module loading and package-style resolution through `require`, including `init.luau` modules and cached module results.

- Embedded modules under the `@lumora/` namespace, including filesystem, process, Luau, standard task, Roblox, datetime, serialization, regular-expression, and analysis helpers.

- Native modules for cryptography, networking, syntax inspection, an isolated VM, and a debugger. The exact native module names are `@lumora/crypto`, `@lumora/net`, `@lumora/syntax`, `@lumora/vm`, and `@lumora/debugger`.

- JSON output for script execution, including captured standard output and error, exit status, duration, script path, and timeout state.

- Optional execution sandboxing that removes selected host capabilities and changes module-loading behavior.

- Static and virtualized analysis through `inspect`, `deobfuscate`, and `report` commands. Analyzer options include JSON output, deterministic analysis, tracing of globals, indexes, calls, and VM activity, plus instruction, event, output, and fixture limits.

- Optional SDL2 visual mode. This mode is compiled only when SDL2, SDL2_ttf, and SDL2_image are available; otherwise the build includes a stub that reports that visual mode is unavailable.

Host operations that need a real Roblox client or engine are not fabricated. The compatibility layer exposes the supported local behavior and returns explicit failures for operations that cannot be implemented in this standalone process.

## Build requirements

The default build uses CMake and Ninja. Lumora's native modules require development packages for cURL, OpenSSL, libsodium, and LZ4. The CI configuration builds on Linux, macOS, and Windows with the corresponding system or vcpkg dependencies.

On Ubuntu, the dependencies can be installed with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
  libcurl4-openssl-dev libssl-dev libsodium-dev liblz4-dev
```

The optional visual build also needs SDL2, SDL2_ttf, and SDL2_image development packages.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
```

The `Makefile` provides the same main workflow:

```bash
make build
```

The executable is written to `build/bin/lumora`. The repository also includes `bin/run`, a small launcher that forwards its arguments to `bin/lumora` when that path exists in the local build layout.

## Run a script

```bash
./build/bin/lumora examples/hello.lua
```

The command accepts script arguments after the script path:

```bash
./build/bin/lumora examples/datatypes.lua one two
```

The CLI syntax is:

```
lumora [run] [--visual] [--no-roblox] [--json] [--sandbox] [--timeout seconds] script.lua [args...]
```

Useful runtime flags are:

- `--no-roblox` disables the Roblox-shaped global layer.

- `--sandbox` applies the runtime's restricted execution environment.

- `--json` emits one machine-readable result envelope instead of ordinary runner output.

- `--timeout seconds` applies a non-negative execution timeout.

- `--visual` starts the SDL2 visual loop. It cannot be combined with `--json` or `--no-roblox`.

- `--version` prints the embedded Lumora version.

## Analyze a script

Lumora exposes analysis commands as top-level commands rather than runtime flags:

```bash
./build/bin/lumora inspect path/to/script.luau
./build/bin/lumora deobfuscate path/to/script.luau --json
./build/bin/lumora report path/to/script.luau --out analysis-output
```

The analyzer also supports deterministic analysis and optional traces. Limits can be supplied with `--instruction-limit`, `--event-limit`, and `--output-limit`; file-backed or virtualized inputs can use `--fixtures` where applicable.

## Examples

The `examples/` directory contains small programs covering basic execution, data types, instance trees, JSON pipelines, attached libraries, and the optional visual lab. The `tests/` directory is more extensive and acts as an executable description of the supported contracts.

For example, the embedded serializer can be used from Luau through the standard module namespace:

```lua
local serde = require("@lumora/serde" )
local encoded = serde.encode({name = "Lumora", enabled = true})
print(encoded)
```

The local compatibility layer can also be used to construct an instance tree:

```lua
local folder = Instance.new("Folder")
local child = Instance.new("Folder", folder)
print(#folder:GetChildren())
folder:Destroy()
```

## Tests

Run the complete CTest suite with:

```bash
ctest --test-dir build --output-on-failure
```

The suite covers CLI behavior, standard modules, `require`, the sandbox, the Roblox-shaped API, scheduling, native crypto and network modules, executor compatibility, JSON schemas, static analysis, and virtualized analysis fixtures. Some tests start local helper servers or use POSIX shell scripts, so the full contract suite is best run on a development environment with the declared dependencies installed.

## Design boundaries

Lumora provides a local runtime and compatibility layer. It does not launch Roblox, connect to a Roblox client, download upstream runtime modules, or manufacture responses for unsupported engine operations. Network access and local process execution are explicit host capabilities, and sandbox mode removes selected capabilities rather than turning a script into a full operating-system isolation boundary.

## License

Lumora is distributed under the MIT License. See [LICENSE](LICENSE) for the complete text.
