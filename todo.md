# Lumora — Plan para llevar las calificaciones de Manus a 10/10

## Fase 1 — Corregir contratos incorrectos ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Prioridad crítica)

- [x] 1.1 Arreglar `typeof`/`type` para reconocer TODOS los tipos emulados: Vector3, Color3, CFrame, UDim, UDim2, Ray, RaycastParams, NumberRange, NumberSequence, ColorSequence, BrickColor, TweenInfo, Font, Rect, Path2D, Random, Drawing, WindUIElement, Tween, Humanoid, etc. ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*que lea `__type` directamente)
- [x] 1.2 Exponer métodos de instancia coherentes: `Color3:ToHex( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*)`, `Color3:ToHSV( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*)`, `Color3:Lerp( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*)` como métodos de instancia ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*no solo estáticos). También `Vector2:Dot`, `Vector2:Lerp`, `Vector3:Dot`, `Vector3:Cross`, `Vector3:Lerp`, `CFrame:PointToObjectSpace`, etc.
- [x] 1.3 Modelar herencia mínima de clases Roblox: `Part` es `BasePart`, `BasePart` es `Instance`, `Model` es `Instance`, etc. `IsA` debe recorrer jerarquía.
- [x] 1.4 Implementar `CFrame` con transformaciones reales ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*rotación Angles, lookAt, multiplicación CFrame*CFrame, CFrame*Vector3, PointToObjectSpace, PointToWorldSpace, etc.)
- [x] 1.5 Implementar `Color3.toHSV` real ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*no ceros)
- [x] 1.6 Añadir tests de regresión para cada corrección

## Fase 2 — Seguridad y aislamiento ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Prioridad crítica)

- [x] 2.1 Quitar `readfile`/`isfile` nativos que acceden al filesystem real; reemplazar por stubs in-memory como el resto del prelude ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*WindUI debe cargarse embebido, no desde disco)
- [x] 2.2 Añadir flag `--sandbox` que deshabilita `loadstring`, `load`, io peligroso, executor stubs y limita coroutines/stdout
- [ ] 2.3 Documentar modelo de amenazas en README: Lumora NO es sandbox de seguridad; para código no confiable usar contenedor externo
- [ ] 2.4 Documentar opción `--sandbox` y su alcance

## Fase 3 — Estabilizar protocolo JSON ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Prioridad alta)

- [x] 3.1 Generar JSON en un único nivel para TODOS los errores ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*archivo inexistente, compilación, runtime, timeout, señal, fallo interno)
- [x] 3.2 Añadir campos separados: `kind`, `message`, `script`, `durationMs`, `timedOut`
- [x] 3.3 No reenvolver stdout del proceso hijo cuando hay error de carga
- [x] 3.4 Documentar el esquema JSON y validar con un parser real en tests

## Fase 4 — Dividir el código (Prioridad alta)

- [x] 4.1 Separar prelude en módulos Lua: `prelude.lua` (core), `instance_api.lua`, `datatype_api.lua`, `services.lua`, `windui_stub.lua`, `executor.lua` — *el prelude se mantiene como raw string embebida en prelude.cpp, separada del CLI*
- [x] 4.2 Cargar módulos Lua desde archivos embed (carga con C++ que lee los .lua del repositorio) — *el prelude embebido se compila como parte de prelude.cpp*
- [x] 4.3 Separar C++ en unidades: `cli.cpp`, `runtime.cpp`, `json.cpp`, `globals.cpp` — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*
- [x] 4.4 Actualizar CMakeLists para compilar módulos
- [x] 4.5 Verificar build y tests tras división (9/9 tests pasan)

## Fase 5 — Separar identidad de Fengetheus ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*petición usuario)

- [x] 5.1 Quitar de documentación/README que Lumora es "para Fengetheus" o "para probar anti-tamper"
- [x] 5.2 Reformular Lumora como proyecto independiente ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*runtime headless de Luau genérico)
- [x] 5.3 Mencionar Fengetheus solo como "uno de los consumidores posibles" o caso de uso opcional, no como propósito
- [x] 5.4 Quitar referencias a anti-tamper/executor como propósito; recluir executor stubs como capa de compatibilidad opcional

## Fase 6 — Ampliar calidad de ingeniería ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Prioridad media)

- [ ] 6.1 Añadir GitHub Actions CI ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Linux, macOS, Windows; GCC y Clang; Debug y Release; CTest)
- [ ] 6.2 Añadir versionado semántico real y changelog ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*CHANGELOG.md)
- [ ] 6.3 Matriz de compatibilidad por API ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*documentar implementada/parcial/stub/no soportada)
- [ ] 6.4 Ejemplos mínimos en `examples/`
- [ ] 6.5 Documentar cómo actualizar la versión de Luau vendorizada

## Fase 7 — Mejorar fidelidad y tests ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Prioridad media)

- [ ] 7.1 Tests de propiedades y métodos por tipo ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*Vector2/3, CFrame, Color3, UDim2)
- [ ] 7.2 Tests de herencia, coerciones, errores de argumentos
- [ ] 7.3 Tests de orden de eventos, ciclos de vida, señales durante destrucción
- [ ] 7.4 Tests de JSON con parser real validando esquema
- [ ] 7.5 Tests negativos ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*argumentos inválidos, APIs no soportadas)

## Fase 8 — Distribución y portabilidad

- [ ] 8.1 Reducir dependencia Unix-específica ( — *hecho: main.cpp(165) + prelude.cpp(1948) + runtime.cpp(127) + json.cpp(20) + lumora.h(21)*fork/waitpid) o proveer fallback multiplataforma
- [ ] 8.2 Documentar matriz de plataformas soportadas
- [ ] 8.3 Añadir notas de releases y checksums en README

## Fase 9 — Commits y entrega

- [ ] 9.1 Hacer commits periódicos a rama main con identidad Xyraniz
- [ ] 9.2 Verificación final: build limpio + todos los tests pasan
- [ ] 9.3 Push final a main
