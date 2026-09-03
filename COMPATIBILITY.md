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
| `ChildAdded` / `ChildRemoved` | ✅ | Se disparan al reparentear y destruir. |

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
| `spawn` / `delay` / `wait` (globals) | ✅ | Aliases de `task.*`. |

## Funciones de entorno

| API | Estado | Notas |
| --- | --- | --- |
| `typeof(v)` | ✅ | Retorna el `__type` verbatim para tipos emulados. |
| `type(v)` | ✅ | Retorna `"userdata"` para tipos Roblox, igual que Roblox. |
| `iscclosure` / `islclosure` | ✅ | |
| `newcclosure` / `clonefunction` | ✅ | |
| `getfenv` / `setfenv` | ✅ | |
| `getgenv` / `getrenv` | 🟡 | Devuelve una tabla compartida; sin aislamiento real de entornos. |
| `loadstring` / `load` | ✅ | Compila Luau a bytecode y carga. Eliminado en `--sandbox`. |
| Capa de executor (`hookfunction`, etc.) | 🟡 | Stubs seguros que no hacen nada; presentes para compatibilidad. Eliminados en `--sandbox`. |

## CLI y salida

| API | Estado | Notas |
| --- | --- | --- |
| `--json` | ✅ | Esquema enriquecido de un solo nivel. |
| `--sandbox` | ✅ | Reduce la superficie de globals peligrosos. |
| `--timeout seconds` | ✅ | Timeout cooperativo en Luau + barrera a nivel de proceso. |
| `--no-roblox` | ✅ | Ejecuta Luau puro sin el prelude. |
| `--help` / `--version` | ✅ | |
| Códigos de salida | ✅ | `0` éxito, `1` error de script/timeout, `2` error de carga/invocación. |
