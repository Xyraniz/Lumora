<div align="center">
  <img src="assets/lumora-logo.png" alt="Lumora logo" width="180" />
  <h1>Lumora</h1>
  <p><strong>Un runtime headless de Luau para ejecutar, inspeccionar y validar scripts fuera de Roblox Studio.</strong></p>
  <p>
    <a href="https://github.com/Xyraniz/Lumora/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Xyraniz/Lumora?style=flat-square" alt="MIT License" /></a>
    <a href="https://github.com/Xyraniz/Lumora"><img src="https://img.shields.io/github/languages/top/Xyraniz/Lumora?style=flat-square" alt="Top language" /></a>
    <a href="https://github.com/Xyraniz/Lumora/issues"><img src="https://img.shields.io/github/issues/Xyraniz/Lumora?style=flat-square" alt="Issues" /></a>
  </p>
</div>

Lumora es un ejecutable autocontenido construido sobre los fuentes oficiales de [Luau](https://luau.org). Su objetivo es ofrecer una superficie de ejecución **headless, reproducible y automatizable** para scripts `.lua` y `.luau` que necesitan una capa compatible con patrones frecuentes de Roblox, sin depender de Roblox Studio, una ventana gráfica o un cliente del juego.

El proyecto combina la VM y el compilador de Luau vendorizados en `vendor/luau` con un prelude aislado de compatibilidad Roblox. El resultado es una herramienta pequeña y directa para pruebas de regresión, validación de output generado, análisis de scripts y pipelines de CI. Lumora es un proyecto independiente: cualquier herramienta o flujo que produzca archivos `.lua` o `.luau` puede consumirlo, sin acoplarse a un generador concreto.

> **Lumora no es Roblox Studio ni un motor 3D.** Emula una superficie headless enfocada en ejecución y validación; no pretende renderizar experiencias, conectarse a servicios reales ni ejecutar un juego completo.

## Por qué Lumora

Los runtimes independientes de Luau suelen enfocarse en programación general o en ofrecer una experiencia de scripting completa. Lumora toma una dirección más específica: prioriza la **compatibilidad práctica con scripts que esperan primitivas Roblox**, junto con una CLI estable para automatización. La herramienta arranca con el entorno Roblox emulado por defecto, permite desactivarlo para probar Luau puro y ofrece salida JSON para integrarse sin ambigüedades con otros procesos.

| Necesidad | Respuesta de Lumora |
| --- | --- |
| Ejecutar Luau sin Roblox Studio | VM y compilador oficiales de Luau integrados en un binario local. |
| Validar scripts que usan primitivas Roblox | Prelude headless con `game`, `workspace`, `Instance`, `Enum`, `task`, tipos y servicios frecuentes. |
| Integrar ejecución en CI o pipelines | CLI sin interfaz gráfica, códigos de salida previsibles y modo `--json`. |
| Evitar procesos que se quedan bloqueados | Timeout cooperativo en Luau y barrera de seguridad a nivel de proceso. |
| Probar comportamientos deterministas | RNG basado en PCG32 y suites de contrato para APIs emuladas. |

## Características principales

Lumora acepta archivos `.lua` y `.luau` directamente, conserva la biblioteca estándar de Luau y soporta sintaxis moderna del compilador. La capa Roblox incluye jerarquías de instancias, servicios, atributos, señales, enumeraciones, tipos de datos y un scheduler cooperativo reducido. Las funciones que dependen de un cliente, una ventana o una red real se mantienen como stubs seguros o comportamientos headless explícitos.

La salida normal conserva el stdout del script. Con `--json`, Lumora devuelve un objeto estructurado con el resultado de la ejecución, stdout, stderr, error y código de salida, lo que permite consumirlo desde scripts de shell, runners de pruebas, pipelines de CI o herramientas escritas en otros lenguajes.

## Instalación y compilación

### Requisitos

Se necesita un compilador C++17, [CMake](https://cmake.org) y [Ninja](https://ninja-build.org). Los fuentes de Luau ya están incluidos en el repositorio, por lo que el build no requiere instalar Luau por separado ni descargar dependencias durante la compilación.

En Debian o Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

### Build reproducible

Desde la raíz del repositorio:

```bash
make
```

El ejecutable recomendado queda disponible en `bin/lumora`. El build también crea `bin/luau-vm` como alias de compatibilidad para scripts antiguos y conserva `bin/run` como lanzador conveniente.

```bash
./bin/lumora script.lua argumento1 argumento2
./bin/run script.lua argumento1 argumento2
```

## Uso de la CLI

```text
lumora [--no-roblox] [--json] [--sandbox] [--timeout seconds] script.lua [args...]
```

El modo Roblox headless está activado por defecto. `--no-roblox` omite el prelude y ejecuta el archivo con Luau puro. `--json` captura la ejecución y escribe un único objeto JSON con el esquema documentado más abajo. `--sandbox` deshabilita los globals peligrosos (`loadstring`, `load`, `os`, `io`, hooks de executor y los stubs de filesystem) y limita el scheduler a 10 ciclos, útil para acotar la superficie de scripts semi-confiables dentro de un pipeline. `--timeout 5` limita la ejecución a cinco segundos y evita que un bucle infinito bloquee el pipeline. `--help` y `--version` no ejecutan ningún script.

Los argumentos siguen la convención habitual de Lua: `arg[0]` contiene la ruta del script y `arg[1]` en adelante contienen los argumentos proporcionados por el usuario.

### Ejemplos

Ejecutar un script con la capa Roblox:

```bash
./bin/lumora examples/hello.lua
```

Ejecutar Luau sin globals Roblox:

```bash
./bin/lumora --no-roblox tests/luau_modern.lua
```

Consumir una respuesta estructurada:

```bash
./bin/lumora --json tests/roblox_api.lua
```

Proteger un runner contra loops que no terminan:

```bash
./bin/lumora --json --timeout 2 generated.luau
```

Una ejecución correcta produce `exitCode: 0` y `ok: true`. Los errores del script producen una respuesta con `ok: false`; las opciones inválidas o errores de invocación de la CLI utilizan código de salida `2`.

### Esquema JSON

Con `--json`, Lumora escribe siempre un único objeto JSON plano en stdout, sin anidar JSON dentro de `stdout`. El mismo esquema se aplica a todos los resultados (éxito, error de carga, error de compilación, error de runtime, timeout y error de invocación), de modo que cualquier consumidor puede leerlo de forma uniforme.

```json
{
  "kind": "success",
  "ok": true,
  "stdout": "hello-world\n",
  "stderr": "",
  "message": "",
  "exitCode": 0,
  "durationMs": 12,
  "timedOut": false,
  "script": "tests/roblox_api.lua"
}
```

| Campo | Tipo | Descripción |
| --- | --- | --- |
| `kind` | string | Categoría del resultado: `success`, `load-error`, `compile-error`, `script-error`, `timeout` o `invocation-error`. |
| `ok` | bool | `true` cuando el script terminó con código de salida `0`. |
| `stdout` | string | Salida estándar íntegra del script, como texto plano (nunca JSON anidado). |
| `stderr` | string | Salida de error íntegra del script. |
| `message` | string | Mensaje legible del fallo; vacío en éxito. |
| `exitCode` | int | Código de salida del proceso: `0` (éxito), `1` (error de script/timeout), `2` (error de carga o invocación). |
| `durationMs` | int | Duración de la ejecución en milisegundos, redondeada. |
| `timedOut` | bool | `true` si la ejecución se interrumpió por `--timeout`. |
| `script` | string | Ruta del script pasada a Lumora. |

El test `tests/json_schema.sh` valida este esquema con el módulo `json` de Python como parser real, comprobando que cada ruta de error produce un objeto plano con todos los campos, que `stdout` nunca contiene JSON anidado y que `timedOut` se distingue de un error de script ordinario.

## Superficie Roblox emulada

La tabla siguiente resume la API cubierta por el prelude actual. La compatibilidad es deliberadamente **headless**: las operaciones sin equivalente local se representan con stubs, valores seguros o colecciones vacías en lugar de intentar conectarse a servicios externos.

| Área | Superficie disponible |
| --- | --- |
| Árbol de instancias | `game`, `workspace`, `Instance.new`, `GetService`, `GetChildren`, `FindFirstChild`, `FindFirstChildOfClass`, `WaitForChild`, `GetFullName`, `Destroy`, `IsA`. |
| Jerarquía | `Parent`, reparenting sin duplicados, `ChildAdded`, `ChildRemoved` y destrucción recursiva. |
| Eventos y señales | `Connect`, `Once`, `Disconnect`, `Connected`, `DisconnectAll`, `Fire`, `AttributeChanged`. |
| Atributos | `GetAttribute` y `SetAttribute`. |
| Enumeraciones | `Enum.X.Y`, `Name`, `EnumType`, `FromName`, `FromValue` y `Value`. |
| Tipos de datos | `typeof`, `Vector2`, `Vector3`, `UDim`, `UDim2`, `CFrame`, `Color3`, `BrickColor`, `Ray`, `RaycastParams`, `NumberRange`, `NumberSequence`, `ColorSequence`, `Font`, `Rect`, `Path2D` y `TweenInfo`. |
| Scheduling | `task.spawn`, `task.defer`, `task.delay`, `task.cancel`, `task.wait`, además de los aliases globales habituales. |
| Funciones de entorno | `iscclosure`, `islclosure`, `newcclosure`, `clonefunction`, `getfenv`, `setfenv`, `getgenv`, `getrenv` y una capa de compatibilidad de executor (stubs seguros). |
| Aleatoriedad | `Random.new(seed)`, `NextInteger`, `NextNumber`, `NextUnitVector` y `Clone`, con estado PCG32 determinista. |
| Biblioteca Luau | Biblioteca estándar, `bit32`, `string.pack/unpack`, `buffer`, `utf8` y sintaxis moderna del compilador. |

La implementación se mantiene en un prelude aislado dentro de `src/main.cpp`. Esta decisión permite ampliar la superficie de compatibilidad sin modificar la VM vendorizada ni acoplarla a un cliente gráfico. La paridad exacta con una versión concreta de Roblox debe comprobarse mediante vectores dorados de esa versión; Lumora prioriza la compatibilidad observable que necesitan sus tests y pipelines.

## Arquitectura del repositorio

| Ruta | Responsabilidad |
| --- | --- |
| `src/main.cpp` | CLI, compilación y ejecución Luau, prelude Roblox, captura JSON, timeout y códigos de salida. |
| `vendor/luau` | Fuentes oficiales vendorizados de Luau, incluyendo VM, compilador y biblioteca común. |
| `tests/` | Smoke tests, contratos de CLI, sintaxis moderna, API Roblox, jerarquía, señales y scheduling. |
| `CMakeLists.txt` | Target C++17, integración de Luau, generación del ejecutable y registro de CTest. |
| `Makefile` | Atajos reproducibles para compilar, probar y limpiar. |
| `bin/run` | Lanzador que resuelve la raíz del repositorio y delega en `bin/lumora`. |
| `assets/lumora-logo.png` | Marca visual principal del proyecto para README, documentación y distribución. |

El flujo de ejecución es intencionalmente simple:

```text
script.lua / script.luau
        │
        ▼
CLI de Lumora ──► compilador Luau ──► VM Luau
        │                              │
        │                              ├─ Prelude Roblox headless
        │                              ├─ Scheduler cooperativo
        │                              └─ Timeout / interrupción
        ▼
stdout, stderr, código de salida o resultado JSON
```

## Pruebas

La suite se ejecuta con:

```bash
make test
```

El equivalente directo es:

```bash
ctest --test-dir build --output-on-failure
```

Las pruebas cubren argumentos y stdout, `--json`, timeouts, sintaxis moderna, biblioteca estándar, jerarquía y reparenting de instancias, eventos de alta y baja de hijos, atributos, enumeraciones, fidelidad de tipos de datos (Vector2/3, CFrame, Color3, UDim2), herencia de clases con `IsA`, destrucción recursiva, llamadas con `:`, scheduling, cancelación básica, modo `--sandbox` y validación del esquema JSON con un parser real.

## Integración con otras herramientas

Lumora es un proyecto independiente y no pertenece a ningún generador de código en particular. Cualquier herramienta que produzca archivos `.lua` o `.luau` puede usar Lumora como etapa de ejecución: recibe el archivo, prepara un entorno compatible y produce un resultado reproducible para tests, validación de output y automatización. Por ejemplo, un generador de código como [Fengetheus](https://github.com/Xyraniz/Fengetheus) puede delegar la ejecución en Lumora, pero la integración es opcional y Lumora funciona igual de bien con scripts escritos a mano o generados por cualquier otra herramienta.

## Modelo de seguridad

Lumora ejecuta c\u00f3digo Luau con acceso a la biblioteca est\u00e1ndar completa y, por defecto, a globals adicionales como `loadstring` y la capa de compatibilidad de executor. **Lumora no es un sandbox de seguridad.** Est\u00e1 dise\u00f1ado para ejecutar scripts sobre los que se tiene control o confianza razonable dentro de un pipeline de CI o un flujo de validaci\u00f3n local. Para ejecutar c\u00f3digo no confiable o de origen desconocido, se debe usar un contenedor externo (Docker, namespaces de Linux, VM, etc.) que a\u00edsla el sistema de archivos, la red y los procesos.

### Modo `--sandbox`

El flag `--sandbox` reduce la superficie disponible para el script, \u00fatil cuando se procesan scripts semi-confiables dentro de un pipeline y se quiere fallar r\u00e1pido ante intentos de acceso a primitivas peligrosas. Concretamente, `--sandbox`:

- Elimina `loadstring` y `load` (no se puede compilar c\u00f3digo arbitrario en tiempo de ejecuci\u00f3n).
- Elimina las bibliotecas `os` e `io` (no hay acceso al sistema de archivos ni al entorno del proceso).
- Elimina los hooks de executor y los stubs de filesystem del prelude.
- Limita el scheduler cooperativo a 10 ciclos, acotando el trabajo que un script puede encolar.

`--sandbox` no sustituye al aislamiento del sistema operativo: es una capa de reducci\u00f3n de superficie dentro del proceso, no una barrera de seguridad completa. El test `tests/sandbox_contract.sh` verifica que los globals peligrosos est\u00e9n ausentes en modo `--sandbox` y presentes en el modo normal.

## Alcance y no objetivos

Lumora está pensado para ejecutar y validar scripts en un entorno local, no para reemplazar Roblox. No renderiza UI, no simula el motor físico, no abre una ventana, no ofrece conectividad Roblox real y no garantiza que una experiencia completa funcione fuera de su plataforma. Las APIs que requieren estado externo se emulan de forma segura y deben tratarse como contratos de compatibilidad, no como acceso a servicios productivos.

## Contribuir

Las contribuciones deben incluir una explicación del comportamiento esperado, una prueba de regresión cuando sea posible y una descripción clara de cualquier diferencia respecto a Luau o Roblox. Para cambios en el prelude, conviene añadir un caso pequeño y determinista a `tests/` antes de ampliar la superficie. Los pull requests que cambien la CLI deben conservar los códigos de salida y el formato JSON documentados en este archivo.

## Licencia

Lumora se distribuye bajo la [licencia MIT](LICENSE). Los fuentes vendorizados de Luau conservan sus avisos y condiciones originales dentro de `vendor/luau`.

## Referencias

[1]: https://luau.org "Luau"
[2]: https://github.com/lune-org/lune "Lune — standalone Luau runtime"
[3]: https://github.com/luau-lang/lute "Lute — standalone Luau runtime for general-purpose programming"
