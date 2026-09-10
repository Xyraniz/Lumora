# Matriz de compatibilidad

Esta matriz documenta el estado de implementación de cada área de la API Roblox emulada por Lumora. El objetivo no es la paridad exhaustiva con una versión concreta de Roblox, sino una compatibilidad observable y reproducible suficiente para ejecutar y validar scripts en pipelines headless.

**Leyenda:**

| Estado | Significado |
| --- | --- |
| ✅ Implementado | La API funciona con semántica equivalente a Roblox. |
| 🟡 Parcial | La API existe y no falla, pero algunas operaciones son stubs o devuelven valores por defecto. |
| 🔴 No soportado | La API no está disponible; llamarla produce un error explícito. |

## Instancias y jerarquía

| API | Estado | Notas |
| --- | --- | --- |
| `Instance.new(className)` | ✅ | Crea instancias con `ClassName` y `Name`. |
| `game`, `workspace` | ✅ | Raíces del árbol de instancias. |
| `GetService(name)` | ✅ | Devuelve servicios singleton del prelude. |
| `GetChildren()` | ✅ | Lista de hijos directos. |
| `FindFirstChild(name)` | ✅ | Búsqueda por nombre entre hijos directos. |
| `FindFirstChildOfClass(className)` | ✅ | Búsqueda por clase entre hijos directos. |
| `WaitForChild(name)` | ✅ | Retorna inmediatamente si existe; timeout no bloquea en modo headless. |
| `GetFullName()` | ✅ | Construye la ruta `game.Service.Instance`. |
| `Destroy()` | ✅ | Destrucción recursiva con `ChildRemoved`. |
| `IsA(className)` | ✅ | Recorre la jerarquía completa de clases (~80 entradas). |
| `Parent` | ✅ | Reparenting sin duplicados; dispara `ChildAdded`/`ChildRemoved`. |
| `Attributes` | 🟡 | `GetAttribute`/`SetAttribute` funcionan en memoria; sin persistencia. |

## Señales y eventos

| API | Estado | Notas |
| --- | --- | --- |
| `Signal:Connect(callback)` | ✅ | Registra un callback. |
| `Signal:Once(callback)` | ✅ | Registra un callback que se desconecta tras la primera invocación. |
| `Connection:Disconnect()` | ✅ | Desconecta el callback. |
| `Connection.Connected` | ✅ | Estado de la conexión. |
| `Signal:DisconnectAll()` | ✅ | Desconecta todos los callbacks. |
| `Signal:Fire(...)` | ✅ | Invoca todos los callbacks conectados. |
| `AttributeChanged` | ✅ | Se dispara al cambiar un atributo. |
| `GetPropertyChangedSignal(name)` | ✅ | Señal reutilizable; se dispara cuando cambia la propiedad observada. |
| `ChildAdded` / `ChildRemoved` | ✅ | Se disparan al reparentear y destruir. |

## Simulación headless de jugadores

| API | Estado | Notas |
| --- | --- | --- |
| `lumora.simulatePlayers(specs)` | ✅ | Agrega jugadores locales deterministas con `Name`, `DisplayName`, `UserId`, `Team`, `Character`, `Head` y `HumanoidRootPart`; dispara `Players.PlayerAdded`. No conecta con Roblox. |
| `lumora.resetSimulatedPlayers()` | ✅ | Elimina los jugadores simulados y conserva `Players.LocalPlayer`; dispara `Players.PlayerRemoving`. |
| `Player.CharacterAdded` / `CharacterRemoving` | ✅ | Señales disponibles en jugadores simulados para probar ciclos de respawn. |
| `Highlight` / `BillboardGui` | 🟡 | Instancias headless con propiedades observables; no renderizan. Son suficientes para validar que un ESP crea, enlaza y habilita sus marcadores. |

## Tipos de datos

| API | Estado | Notas |
| --- | --- | --- |
| `Vector2.new(x, y)` | ✅ | Aritmética, `Dot`, `Lerp`, `Cross`, `Angle`, `Magnitude`, `Unit`. |
| `Vector3.new(x, y, z)` | ✅ | Aritmética, `Dot`, `Cross`, `Lerp`, `Angle`, `Magnitude`, `Unit`. |
| `CFrame.new(...)` | ✅ | Matriz real: composición, transformación, `Angles`, `lookAt`, `Inverse`, `PointToObjectSpace`, `PointToWorldSpace`, `VectorToObjectSpace`, `VectorToWorldSpace`. |
| `Color3.new(r, g, b)` / `fromRGB` / `fromHSV` | ✅ | `ToHex`, `ToHSV`, `Lerp` como métodos de instancia. |
| `UDim.new(scale, offset)` | ✅ | |
| `UDim2.new(sx, ox, sy, oy)` | ✅ | `fromScale`, `fromOffset`, `__tostring`. |
| `BrickColor.new(...)` | ✅ | Paleta de colores por nombre o número. |
| `Ray.new(origin, direction)` | 🟡 | Estructura presente; `Raycast` devuelve un resultado stub. |
| `RaycastParams.new()` | 🟡 | Estructura presente; no afecta el resultado del raycast. |
| `NumberRange.new(min, max)` | ✅ | |
| `NumberSequence.new(...)` | ✅ | Keypoints con `Envelope` y `Time`/`Value`. |
| `ColorSequence.new(...)` | ✅ | Keypoints con `Time` y `Color`. |
| `Font.new(...)` | ✅ | |
| `Rect.new(...)` | ✅ | |
| `Path2D.new()` | 🟡 | Estructura con `ControlPoints`; sin renderizado. |
| `TweenInfo.new(...)` | ✅ | `EasingStyle`, `EasingDirection`, `Duration`, etc. |
| `Random.new(seed)` | ✅ | PCG32 determinista; `NextInteger`, `NextNumber`, `NextUnitVector`, `Clone`. |
| `Tween` | 🟡 | Estructura presente; sin animación real. |

## Enumeraciones

| API | Estado | Notas |
| --- | --- | --- |
| `Enum.X.Y` | ✅ | Acceso por nombre con `__index` diferida. |
| `EnumItem.Name` / `.Value` | ✅ | |
| `EnumItem.EnumType` | ✅ | Referencia al `Enum` padre. |
| `Enum.FromName` / `FromValue` | ✅ | |
| `EnumType.FromName` / `FromValue` | ✅ | |

## Scheduling

| API | Estado | Notas |
| --- | --- | --- |
| `task.spawn(fn, ...)` | ✅ | Ejecuta inmediatamente y encola la continuación. |
| `task.defer(fn, ...)` | ✅ | Difiere al final del ciclo actual. |
| `task.delay(seconds, fn, ...)` | ✅ | Programa con retardo simulado. |
| `task.cancel(thread)` | ✅ | Cancela un thread encolado. |
| `task.wait(seconds)` | 🟡 | Retorna inmediatamente sin bloquear real. |
| `task.resume(thread)` / `task.deferSelf()` | ✅ | Reanuda un thread suspendido o difiere el thread actual al siguiente ciclo cooperativo. |
| Errores de tareas | ✅ | Los errores de threads desconectados se devuelven por `task._runScheduler()` y hacen fallar la ejecución si el runtime los drena sin consumirlos. |
| `spawn` / `delay` / `wait` (globals) | ✅ | Aliases de `task.*`. |

## Módulos Luau

| API | Estado | Notas |
| --- | --- | --- |
| `require("./module")` | ✅ | Loader oficial `Luau.Require`; resuelve relativo al chunk actual, `.luau` antes que `.lua`, y conserva caché por ejecución. |
| `require("./package")` | ✅ | Resuelve `package/init.luau` o `package/init.lua`. |
| `require` en `--sandbox` | 🔴 | Se elimina junto con la carga dinámica y los stubs de filesystem para no ejecutar módulos host desde el entorno reducido. |

## Funciones de entorno

| API | Estado | Notas |
| --- | --- | --- |
| `typeof(v)` | ✅ | Retorna el `__type` verbatim para tipos emulados. |
| `type(v)` | ✅ | Retorna `"userdata"` para tipos Roblox, igual que Roblox. |
| `iscclosure` / `islclosure` | ✅ | |
| `newcclosure` / `clonefunction` | ✅ | |
| `getfenv` / `setfenv` | ✅ | |
| `getgenv` / `getrenv` | 🟡 | Devuelve una tabla compartida; sin aislamiento real de entornos. |
| `setclipboard` / `getclipboard` | ✅ | Clipboard en memoria por defecto; backend del sistema opt-in mediante `LUMORA_SYSTEM_CLIPBOARD=1`, con fallback seguro. Eliminado en `--sandbox`. |
| `getcallstack` | ✅ | Devuelve frames serializables con fuente, línea, nombre y tipo. Eliminado en `--sandbox`. |
| `lumora.capabilities()` | ✅ | Describe capacidades locales (`memory`, `stub`, `disabled`, `headless`). Eliminado en `--sandbox`. |
| `loadstring` / `load` | ✅ | Compila Luau a bytecode y carga. Presente también en `--no-roblox` (paridad con el CLI oficial, que registra `loadstring` y `collectgarbage` como globals). Eliminado en `--sandbox`. |
| Bibliotecas congeladas | ✅ | Escribir en `string`, `table`, `math`, `os`, `coroutine`, `debug`, `utf8`, `bit32`, `buffer`, `vector` o en `getmetatable("")` produce `attempt to modify a readonly table`, igual que en Roblox real y el CLI oficial. El entorno de globals sigue escribible (paridad con Roblox). |
| Capa de executor (`hookfunction`, etc.) | 🟡 | Solo wrappers/inspección segura; hooks mutantes no están disponibles y no reportan mutación. Eliminados en `--sandbox`. |

## Filesystem y serialización

| API | Estado | Notas |
| --- | --- | --- |
| `writefile` / `readfile` / `appendfile` | ✅ | Contenido efímero en memoria; `readfile` falla si el archivo no existe. |
| `isfile` / `isfolder` / `makefolder` | ✅ | Directorios virtuales creados en memoria. |
| `delfile` / `delfolder` / `listfiles` | ✅ | Eliminación y listado determinista de entradas virtuales. |
| `loadfile` | ✅ | Compila desde el filesystem virtual y usa el nombre lógico como chunk name. |
| `HttpService:JSONEncode` / `JSONDecode` | ✅ | Codec JSON nativo con arrays/objetos, escapes, límites de profundidad y detección de ciclos. |
| `json.encode` / `json.decode` | ✅ | Alias sin estado del codec de `HttpService`. |

## Servicios headless y capacidades externas

| API | Estado | Notas |
| --- | --- | --- |
| `TweenService:Create` | ✅ | Aplica propiedades, mantiene `PlaybackState` y dispara `Completed`; no anima en tiempo real. |
| `CollectionService` | ✅ | Tags, consultas y señales de alta/baja en memoria. |
| `Camera:WorldToViewportPoint` / `ViewportPointToRay` | ✅ | Proyección perspectiva determinista basada en `CFrame`, FOV y viewport. |
| `VirtualInputManager` / `ContextActionService` | ✅ | Eventos y acciones observables en memoria; no inyectan eventos al sistema operativo. |
| `Debris:AddItem` / `StarterGui` | ✅ | Scheduler de destrucción y estado de CoreGui/Core funcionales en memoria. |
| `request` / `HttpService` de red | 🔴 | No hay transporte de red; las llamadas producen un error explícito. JSON y `UrlEncode` sí son locales. |
| `TeleportService` | 🔴 | No existe cliente Roblox; las llamadas producen un error explícito. |

## Laboratorio visual nativo

| Área | Estado | Notas |
| --- | --- | --- |
| `lumora --visual script.lua` | ✅ | Ejecuta el mismo prelude Luau dentro de una ventana SDL2 y conserva el modo headless sin cambios. |
| Mundo 3D | ✅ | Piso, muros de prueba, cámara en primera persona y proyección 3D determinista. |
| Entrada y ciclo | ✅ | WASD mueve la cámara, mouse rota la vista, `Esc` cierra, `F` activa selección y cada frame dispara `Heartbeat`/`RenderStepped`. |
| Jugadores simulados | ✅ | Los jugadores y personajes proceden del árbol real creado por Luau; sus posiciones se proyectan y renderizan. |
| `Highlight` | ✅ | El renderer detecta `Highlight` habilitados bajo cada personaje y dibuja caja, color y línea de ESP. |
| `Drawing.Line`/`Drawing.Text` | ✅ | Los objetos creados por Luau se consultan cada frame y se dibujan en pantalla. |
| Oclusión y selección | ✅ | Los muros de prueba bloquean líneas ESP y el selector ignora objetivos ocluidos. |
| `BillboardGui`/texto | 🟡 | Las etiquetas de jugador se renderizan como HUD nativo; el layout completo de Roblox aún no se reproduce. |
| Física Roblox | 🔴 | El laboratorio usa movimiento de cámara, oclusión y posiciones deterministas; no intenta sustituir el motor físico completo de Roblox. |

## Fidelidad de UI

| Área | Estado | Notas |
| --- | --- | --- |
| Texto | ✅ | SDL_ttf con fuente local rasterizada y métrica real; no se dibuja como bloques. |
| Alfa y esquinas | ✅ | Transparencia por píxel y `UICorner` aproximado mediante geometría nativa. |
| Layout `UDim2` | ✅ | Se resuelven escalas y offsets contra el viewport visual de 1280×720. |
| Assets `rbxassetid://` | 🔴 | No se inventan texturas: requieren un archivo local compatible; Roblox no ofrece esos assets al laboratorio. |
| Paridad píxel a píxel | 🔴 | No es técnicamente garantizable fuera del compositor, fuentes, atlas y rasterizador de Roblox. |

## CLI y salida

| API | Estado | Notas |
| --- | --- | --- |
| `--json` | ✅ | Esquema enriquecido de un solo nivel con `traceback` separado. |
| `--sandbox` | ✅ | Reduce la superficie de globals peligrosos. |
| `--timeout seconds` | ✅ | Timeout cooperativo en Luau + barrera a nivel de proceso. |
| `--no-roblox` | ✅ | Ejecuta Luau puro sin el prelude, con paridad de superficie con el CLI oficial: `loadstring`/`collectgarbage` registrados, chunk names `"@" + normalizePath`, errores con `stacktrace:`, chunk principal en corrutina y tablas de biblioteca congeladas. |
| `--help` / `--version` | ✅ | |
| Códigos de salida | ✅ | `0` éxito, `1` error de script/timeout, `2` error de carga/invocación. |
