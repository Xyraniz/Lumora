# Lumora

Lumora es un runtime headless de Luau para ejecutar y validar scripts `.lua` y `.luau` fuera de Roblox Studio. El binario incorpora la VM y el compilador de Luau, junto con una capa de compatibilidad Roblox orientada a pruebas automatizadas.

El proyecto no es Roblox Studio ni un motor 3D. No abre una experiencia, no se conecta a servicios reales y no promete compatibilidad completa con el cliente de Roblox. Su utilidad está en reproducir ejecuciones, inspeccionar resultados y detectar regresiones en un proceso local o de CI.

## Uso rápido

Requiere un compilador C++17, CMake y Ninja. En Debian o Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
make
./bin/lumora examples/hello.lua
```

La CLI también acepta `--no-roblox` para ejecutar Luau sin el prelude de Roblox, `--json` para devolver un objeto estructurado y `--timeout SECONDS` para limitar ejecuciones. `--sandbox` reduce la superficie disponible para scripts que no deben acceder a filesystem, carga dinámica o APIs externas.

```bash
./bin/lumora --no-roblox tests/luau_modern.lua
./bin/lumora --json examples/json_pipeline.lua
./bin/lumora --json --timeout 2 generated.luau
```

## Qué incluye

La capa headless modela instancias, servicios, atributos, señales, enums, tipos de datos y un scheduler cooperativo. El cargador admite módulos relativos y directorios con `init.lua` o `init.luau`. El modo visual opcional usa SDL2 para el laboratorio incluido en `examples/visual_lab.lua`; el runtime headless no depende de SDL.

Con `--json`, Lumora mantiene un esquema estable con `kind`, `ok`, `stdout`, `stderr`, `message`, `traceback`, `exitCode`, `durationMs`, `timedOut` y `script`. La suite de `tests/` cubre la CLI, la compatibilidad de Luau, la superficie Roblox, el sandbox, los módulos y el esquema JSON.

## Estructura y licencia

El código C++ está en `src/`, los ejemplos en `examples/` y los contratos de prueba en `tests/`. Consulta `COMPATIBILITY.md`, `CHANGELOG.md` y `LICENSE` antes de integrar el runtime en otro proyecto.
