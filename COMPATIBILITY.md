# Compatibility matrix

This matrix documents the implementation status of each area of the Roblox API emulated by Lumora. The goal is not exhaustive parity with a specific Roblox version, but observable and reproducible compatibility sufficient to run and validate scripts in headless pipelines.

## Independent standard modules

Lumora provides `@lumora/datetime`, `@lumora/serde`, `@lumora/task`, and
`@lumora/roblox` as virtual modules implemented inside this runtime. They do
not import Lune code or depend on the Lune repository. The APIs are covered by
`tests/standard_modules_contract.lua`.

**Legend:**

| Status | Meaning |
| --- | --- |
| ✅ Implemented | The API behaves with Roblox-equivalent semantics. |
| 🟡 Partial | The API exists and does not fail, but some operations are stubs or return default values. |
| 🔴 Unsupported | The API is unavailable; calling it produces an explicit error. |

## Instances and hierarchy

| API | Status | Notes |
| --- | --- | --- |
| `Instance.new(className)` | ✅ | Creates instances with `ClassName` and `Name`. |
| `game`, `workspace` | ✅ | Roots of the instance tree. |
| `GetService(name)` | ✅ | Returns singleton services from the prelude. |
| `GetChildren()` | ✅ | Lists direct children. |
| `FindFirstChild(name)` | ✅ | Name-based search among direct children. |
| `FindFirstChildOfClass(className)` | ✅ | Class-based search among direct children. |
| `WaitForChild(name)` | ✅ | Returns immediately if present; timeout does not block in headless mode. |
| `GetFullName()` | ✅ | Builds the `game.Service.Instance` path. |
| `Destroy()` | ✅ | Recursive destruction with `ChildRemoved`. |
| `IsA(className)` | ✅ | Walks the full class hierarchy (~80 entries). |
| `Parent` | ✅ | Reparenting without duplicates; fires `ChildAdded`/`ChildRemoved`. |
| `Attributes` | 🟡 | `GetAttribute`/`SetAttribute` work in memory; no persistence. |

## Signals and events

| API | Status | Notes |
| --- | --- | --- |
| `Signal:Connect(callback)` | ✅ | Registers a callback. |
| `Signal:Once(callback)` | ✅ | Registers a callback that disconnects after the first invocation. |
| `Connection:Disconnect()` | ✅ | Disconnects the callback. |
| `Connection.Connected` | ✅ | Connection state. |
| `Signal:DisconnectAll()` | ✅ | Disconnects all callbacks. |
| `Signal:Fire(...)` | ✅ | Invokes all connected callbacks. |
| `AttributeChanged` | ✅ | Fired when an attribute changes. |
| `GetPropertyChangedSignal(name)` | ✅ | Reusable signal; fired when the observed property changes. |
| `ChildAdded` / `ChildRemoved` | ✅ | Fired on reparent and destroy. |

## Headless player simulation

| API | Status | Notes |
| --- | --- | --- |
| `lumora.simulatePlayers(specs)` | ✅ | Adds deterministic local players with `Name`, `DisplayName`, `UserId`, `Team`, `Character`, `Head` and `HumanoidRootPart`; fires `Players.PlayerAdded`. Does not connect to Roblox. |
| `lumora.resetSimulatedPlayers()` | ✅ | Removes simulated players and preserves `Players.LocalPlayer`; fires `Players.PlayerRemoving`. |
| `Player.CharacterAdded` / `CharacterRemoving` | ✅ | Signals available on simulated players to test respawn cycles. |
| `Highlight` / `BillboardGui` | 🟡 | Headless instances with observable properties; do not render. Sufficient to validate that an ESP creates, binds and enables its markers. |

## Data types

| API | Status | Notes |
| --- | --- | --- |
| `Vector2.new(x, y)` | ✅ | Arithmetic, `Dot`, `Lerp`, `Cross`, `Angle`, `Magnitude`, `Unit`. |
| `Vector3.new(x, y, z)` | ✅ | Arithmetic, `Dot`, `Cross`, `Lerp`, `Angle`, `Magnitude`, `Unit`. |
| `CFrame.new(...)` | ✅ | Real matrix: composition, transformation, `Angles`, `lookAt`, `Inverse`, `PointToObjectSpace`, `PointToWorldSpace`, `VectorToObjectSpace`, `VectorToWorldSpace`. |
| `Color3.new(r, g, b)` / `fromRGB` / `fromHSV` | ✅ | `ToHex`, `ToHSV`, `Lerp` as instance methods. |
| `UDim.new(scale, offset)` | ✅ | |
| `UDim2.new(sx, ox, sy, oy)` | ✅ | `fromScale`, `fromOffset`, `__tostring`. |
| `BrickColor.new(...)` | ✅ | Palette colors by name or number. |
| `Ray.new(origin, direction)` | 🟡 | Structure present; `Raycast` returns a stub result. |
| `RaycastParams.new()` | 🟡 | Structure present; does not affect raycast result. |
| `NumberRange.new(min, max)` | ✅ | |
| `NumberSequence.new(...)` | ✅ | Keypoints with `Envelope` and `Time`/`Value`. |
| `ColorSequence.new(...)` | ✅ | Keypoints with `Time` and `Color`. |
| `Font.new(...)` | ✅ | |
| `Rect.new(...)` | ✅ | |
| `Path2D.new()` | 🟡 | Structure with `ControlPoints`; no rendering. |
| `TweenInfo.new(...)` | ✅ | `EasingStyle`, `EasingDirection`, `Duration`, etc. |
| `Random.new(seed)` | ✅ | Deterministic PCG32; `NextInteger`, `NextNumber`, `NextUnitVector`, `Clone`. |
| `Tween` | 🟡 | Structure present; no real animation. |

## Enumerations

| API | Status | Notes |
| --- | --- | --- |
| `Enum.X.Y` | ✅ | Name-based access with deferred `__index`. |
| `EnumItem.Name` / `.Value` | ✅ | |
| `EnumItem.EnumType` | ✅ | Reference to the parent `Enum`. |
| `Enum.FromName` / `FromValue` | ✅ | |
| `EnumType.FromName` / `FromValue` | ✅ | |

## Scheduling

| API | Status | Notes |
| --- | --- | --- |
| `task.spawn(fn, ...)` | ✅ | Resumes immediately and enqueues the continuation at a cooperative yield. |
| `task.defer(fn, ...)` | ✅ | Enqueues for the next cooperative cycle; top-level calls advance one zero-time cycle. |
| `task.delay(seconds, fn, ...)` | ✅ | Schedules against deterministic virtual time and does not run before its deadline. |
| `task.cancel(thread)` | ✅ | Cancels an enqueued thread. |
| `task.wait(seconds)` | ✅ | Yields the current task until its virtual deadline and returns the requested elapsed duration. |
| `task.resume(thread)` / `task.deferSelf()` | ✅ | Resumes registered or directly-created coroutines, or defers the current task to the next cooperative tick. |
| `task.status(thread)` | ✅ | Reports the coroutine status, including completed and cancelled tasks. |
| Task errors | ✅ | Errors from disconnected threads are returned by `task._runScheduler()` and fail execution if the runtime drains them without consuming. |
| `spawn` / `delay` / `wait` (globals) | ✅ | Aliases for `task.*`. |
| `RunService:BindToRenderStep` / `UnbindFromRenderStep` | ✅ | Callbacks execute in ascending priority order; equal priorities preserve bind order. The visual loop dispatches them before `RenderStepped`. |

## Luau modules

| API | Status | Notes |
| --- | --- | --- |
| `require("./module")` | ✅ | Official `Luau.Require` loader; resolves relative to the current chunk, prefers `.luau` over `.lua`, and caches per execution. |
| `require("./package")` | ✅ | Resolves `package/init.luau` or `package/init.lua`. |
| `require` en `--sandbox` | 🔴 | Removed along with dynamic loading and filesystem stubs to avoid executing host modules from the reduced environment. |

## Environment functions

| API | Status | Notes |
| --- | --- | --- |
| `typeof(v)` | ✅ | Returns the verbatim `__type` for emulated types. |
| `type(v)` | ✅ | Returns `"userdata"` for Roblox types, same as Roblox. |
| `iscclosure` / `islclosure` | ✅ | |
| `newcclosure` / `clonefunction` | ✅ | |
| `getfenv` / `setfenv` | ✅ | |
| `getgenv` / `getrenv` | 🟡 | Returns a shared table; no real environment isolation. |
| `setclipboard` / `getclipboard` | ✅ | In-memory clipboard by default; system backend opt-in via `LUMORA_SYSTEM_CLIPBOARD=1`, with a safe fallback. Removed in `--sandbox`. |
| `getcallstack` | ✅ | Returns serializable frames with source, line, name and type. Removed in `--sandbox`. |
| `lumora.capabilities()` | ✅ | Describes local capabilities (`memory`, `stub`, `disabled`, `headless`). Removed in `--sandbox`. |
| `loadstring` / `load` | ✅ | Compiles Luau to bytecode and loads. Also present in `--no-roblox` (parity with the official CLI, which registers `loadstring` and `collectgarbage` as globals). Removed in `--sandbox`. |
| Frozen libraries | ✅ | Writing to `string`, `table`, `math`, `os`, `coroutine`, `debug`, `utf8`, `bit32`, `buffer`, `vector` or to `getmetatable("")` raises `attempt to modify a readonly table`, matching real Roblox and the official CLI. The globals table remains writable (parity with Roblox). |
| Executor layer (`hookfunction`, etc.) | 🟡 | Only safe wrappers/inspection; mutating hooks are not available and do not report mutation. Removed in `--sandbox`. |

## Filesystem and serialization

| API | Status | Notes |
| --- | --- | --- |
| `writefile` / `readfile` / `appendfile` | ✅ | Ephemeral in-memory content; `readfile` fails if the file does not exist. |
| `isfile` / `isfolder` / `makefolder` | ✅ | Virtual directories created in memory. |
| `delfile` / `delfolder` / `listfiles` | ✅ | Deterministic deletion and listing of virtual entries. |
| `loadfile` | ✅ | Compiles from the virtual filesystem and uses the logical name as the chunk name. |
| `require("@lumora/fs")` | ✅ | Lumora-native virtual filesystem module with `readFile`, `writeFile`, `appendFile`, `readDir`, `metadata`, `isFile`, `isDir`, recursive `copy`, `move`, and `remove`. It never accesses the host filesystem. |
| `HttpService:JSONEncode` / `JSONDecode` | ✅ | Native JSON codec with arrays/objects, escapes, depth limits and cycle detection. |
| `json.encode` / `json.decode` | ✅ | Stateless aliases for `HttpService`'s codec. |

## Headless services and external capabilities

| API | Status | Notes |
| --- | --- | --- |
| `TweenService:Create` | ✅ | Applies properties, maintains `PlaybackState` and fires `Completed`; does not animate in real time. |
| `CollectionService` | ✅ | Tags, queries and add/remove signals in memory. |
| `Camera:WorldToViewportPoint` / `ViewportPointToRay` | ✅ | Deterministic perspective projection based on `CFrame`, FOV and viewport. |
| `VirtualInputManager` / `ContextActionService` | ✅ | Observable events and actions in memory; do not inject events into the OS. |
| `Debris:AddItem` / `StarterGui` | ✅ | Destruction scheduler and CoreGui/Core state functional in memory. |
| `request` / network `HttpService` | 🔴 | No network transport; calls produce an explicit error. JSON and `UrlEncode` are local. |
| `TeleportService` | 🔴 | No Roblox client; calls produce an explicit error. |

## Native visual laboratory

| Area | Status | Notes |
| --- | --- | --- |
| `lumora --visual script.lua` | ✅ | Runs the same Luau prelude inside an SDL2 window and preserves headless mode unchanged. |
| 3D world | ✅ | Floor, test walls, first-person camera and deterministic 3D projection. |
| Input and loop | ✅ | WASD moves the camera, mouse rotates view, `Esc` closes, `F` toggles selection and each frame fires `Heartbeat`/`RenderStepped`. |
| Simulated players | ✅ | Players and characters come from the real tree created by Luau; their positions are projected and rendered. |
| `Highlight` | ✅ | The renderer detects enabled `Highlight` instances under each character and draws box, color and ESP line. |
| `Drawing.Line`/`Drawing.Text` | ✅ | Objects created by Luau are queried each frame and drawn on screen. |
| Occlusion and selection | ✅ | Test walls block ESP lines and the selector ignores occluded targets. |
| `BillboardGui`/text | 🟡 | Player labels render as native HUD; the complete Roblox layout is not yet reproduced. |
| Roblox physics | 🔴 | The lab uses camera movement, occlusion and deterministic positions; it does not attempt to substitute Roblox's full physics engine. |

## UI fidelity

| Area | Status | Notes |
| --- | --- | --- |
| Text | ✅ | SDL_ttf with locally rasterized font and real metrics; not drawn as blocks. |
| Alpha and corners | ✅ | Per-pixel transparency and `UICorner` approximated via native geometry. |
| Layout `UDim2` | ✅ | Scales and offsets are resolved against the visual viewport of 1280×720. |
| Assets `rbxassetid://` | 🔴 | Textures are not fabricated: they require a compatible local file; Roblox does not provide those assets to the lab. |
| Pixel-perfect parity | 🔴 | Not technically guaranteed outside Roblox's compositor, fonts, atlas and rasterizer. |

## CLI and output

| API | Status | Notes |
| --- | --- | --- |
| `--json` | ✅ | Enriched single-level schema with separate `traceback`. |
| `--sandbox` | ✅ | Reduces the surface of dangerous globals. |
| `--timeout seconds` | ✅ | Cooperative timeout in Luau + process-level barrier. |
| `--no-roblox` | ✅ | Runs pure Luau without the prelude, with surface parity to the official CLI: `loadstring`/`collectgarbage` registered, chunk names `"@" + normalizePath`, errors with `stacktrace:`, main chunk in a coroutine and library tables frozen. |
| `--help` / `--version` | ✅ | |
| Exit codes | ✅ | `0` success, `1` script/timeout error, `2` load/invoke error. |
