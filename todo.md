# Lumora — Plan para llevar las calificaciones de Manus a 10/10

## Fase 1 — Corregir contratos incorrectos (Prioridad crítica)

- [x] 1.1 Arreglar `typeof`/`type` para reconocer TODOS los tipos emulados (que lea `__type` directamente)
- [x] 1.2 Exponer métodos de instancia coherentes: `Color3:ToHex`, `Color3:ToHSV`, `Color3:Lerp`, `Vector2:Dot`, `Vector2:Lerp`, `Vector3:Dot`, `Vector3:Cross`, `Vector3:Lerp`, `CFrame:PointToObjectSpace`, etc.
- [x] 1.3 Modelar herencia mínima de clases Roblox: `Part` es `BasePart`, `BasePart` es `Instance`, etc. `IsA` recorre jerarquía.
- [x] 1.4 Implementar `CFrame` con transformaciones reales (Angles, lookAt, CFrame*CFrame, CFrame*Vector3, PointToObjectSpace, PointToWorldSpace, etc.)
- [x] 1.5 Implementar `Color3.toHSV` real (no ceros)
- [x] 1.6 Añadir tests de regresión para cada corrección

## Fase 2 — Seguridad y aislamiento (Prioridad crítica)

- [x] 2.1 Quitar `readfile`/`isfile` nativos que acceden al filesystem real; reemplazar por stubs in-memory
- [x] 2.2 Añadir flag `--sandbox` que deshabilita `loadstring`, `load`, io peligroso, executor stubs y limita coroutines/stdout
- [x] 2.3 Documentar modelo de amenazas en README: Lumora NO es sandbox de seguridad; para código no confiable usar contenedor externo
- [x] 2.4 Documentar opción `--sandbox` y su alcance

## Fase 3 — Estabilizar protocolo JSON (Prioridad alta)

- [x] 3.1 Generar JSON en un único nivel para TODOS los errores
- [x] 3.2 Añadir campos separados: `kind`, `message`, `script`, `durationMs`, `timedOut`
- [x] 3.3 No reenvolver stdout del proceso hijo cuando hay error de carga
- [x] 3.4 Documentar el esquema JSON y validar con un parser real en tests

## Fase 4 — Dividir el código (Prioridad alta)

- [x] 4.1 Separar prelude en módulos (hecho a nivel C++: prelude.cpp/runtime.cpp/json.cpp/main.cpp)
- [x] 4.2 Cargar módulos Lua desde archivos embed (el prelude se mantiene como raw string embebida en prelude.cpp)
- [x] 4.3 Separar C++ en unidades: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)
- [x] 4.4 Actualizar CMakeLists para compilar módulos
- [x] 4.5 Verificar build y tests tras división

## Fase 5 — Separar identidad de Fengetheus (petición usuario)

- [x] 5.1 Quitar de documentación/README que Lumora es "para Fengetheus" o "para probar anti-tamper"
- [x] 5.2 Reformular Lumora como proyecto independiente (runtime headless de Luau genérico)
- [x] 5.3 Mencionar Fengetheus solo como "uno de los consumidores posibles", no como propósito
- [x] 5.4 Quitar referencias a anti-tamper/executor como propósito; recluir executor stubs como capa de compatibilidad opcional

## Fase 6 — Ampliar calidad de ingeniería (Prioridad media)

- [x] 6.1 Añadir GitHub Actions CI (Linux + macOS, GCC y Clang, Debug y Release, CTest)
- [x] 6.2 Añadir versionado semántico real y changelog (CHANGELOG.md)
- [x] 6.3 Matriz de compatibilidad por API (COMPATIBILITY.md)
- [x] 6.4 Ejemplos mínimos en `examples/`
- [x] 6.5 Documentar cómo actualizar la versión de Luau vendorizada

## Fase 7 — Mejorar fidelidad y tests (Prioridad media)

- [x] 7.1 Tests de propiedades y métodos por tipo (Vector2/3, CFrame, Color3, UDim2) — tests/properties_contract.lua
- [x] 7.2 Tests de herencia, coerciones, errores de argumentos — cubierto en datatypes_contract.lua y negative_contract.lua
- [x] 7.3 Tests de orden de eventos, ciclos de vida, señales durante destrucción — tests/events_contract.lua
- [x] 7.4 Tests de JSON con parser real validando esquema — tests/json_schema.sh
- [x] 7.5 Tests negativos (argumentos inválidos, APIs no soportadas) — tests/negative_contract.lua

## Fase 8 — Distribución y portabilidad

- [x] 8.1 Reducir dependencia Unix-específica (fork/waitpid) o proveer fallback multiplataforma
- [ ] 8.2 Documentar matriz de plataformas soportadas
- [ ] 8.3 Añadir notas de releases y checksums en README

## Fase 9 — Commits y entrega

- [x] 9.1 Hacer commits periódicos a rama main con identidad Xyraniz
- [ ] 9.2 Verificacián final: build limpio + todos los tests pasan
- [ ] 9.3 Push final a main
