# Lumora

Lumora es un runtime autocontenido de **Luau** para ejecutar y validar el output generado por Fengetheus fuera de Roblox Studio. Usa los fuentes oficiales de Luau vendorizados en `vendor/luau`, añade una capa headless de compatibilidad Roblox y mantiene una interfaz CLI reproducible para automatización y pruebas.

## Requisitos y build reproducible

En Debian/Ubuntu se necesita un compilador C++17, CMake y Ninja. No se requiere una instalación del sistema de Luau ni una descarga durante el build.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
make
make test
```

El ejecutable principal y nombre recomendado son `bin/lumora`. También se genera `bin/luau-vm` únicamente como alias de compatibilidad para scripts existentes que aún usan ese nombre; los nuevos comandos y pipelines deben invocar `lumora`. El lanzador `bin/run` delega al ejecutable principal, por lo que ambos estilos son válidos:

```bash
./bin/lumora script.lua argumento1 argumento2
./bin/run script.lua argumento1 argumento2
```

## CLI

```text
lumora [--no-roblox] [--json] [--timeout seconds] script.lua [args...]
```

`--no-roblox` omite el prelude de compatibilidad y permite probar Luau puro. `--json` devuelve `{ok, stdout, stderr, error, exitCode}` y captura la ejecución en un proceso aislado. `--timeout 5` instala una interrupción cooperativa en Luau y una barrera de seguridad de proceso para que un `while true do end` no bloquee el pipeline. `--help` y `--version` son operaciones sin efectos secundarios.

Los argumentos se exponen con la convención habitual de Lua: `arg[0]` contiene la ruta del script y `arg[1]` en adelante contienen los argumentos del usuario.

## API emulada

| Área | Superficie disponible |
| --- | --- |
| Árbol Roblox | `game`, `workspace`, `Instance.new`, `GetService`, `GetChildren`, `FindFirstChild`, `WaitForChild`, `GetFullName`, `Destroy`, `IsA` |
| Jerarquía | `Parent`, reparenting sin duplicados, `ChildAdded`, `ChildRemoved` |
| Eventos | `Connect`, `Disconnect`, `Connected`, `Fire`, `AttributeChanged` |
| Atributos | `GetAttribute`, `SetAttribute` |
| Enumeraciones | `Enum.X.Y`, `Name`, `EnumType`, `FromName`, `FromValue`, `Value` |
| Tipos | `typeof`, `Vector2.new`, `UDim2.new`, `UDim2.fromScale`, `UDim2.fromOffset`, `Path2D.new` |
| Scheduling | `task.spawn`, `task.defer`, `task.delay`, `task.cancel`, `task.wait` |
| Funciones | `iscclosure`, `islclosure`, `newcclosure`, `clonefunction`, `getfenv`, `setfenv` |
| Random | `Random.new(seed)`, `NextInteger`, `NextNumber`, `NextUnitVector`, `Clone`, con estado PCG32 determinista |
| Biblioteca | Biblioteca estándar Luau, `bit32`, `string.pack/unpack`, `buffer`, `utf8` y sintaxis moderna del compilador |

La API está implementada en un prelude aislado para que los casos generados por Fengetheus puedan extenderse sin modificar la VM. El RNG usa la familia PCG32 y la inicialización/transformación documentadas en el runtime oficial de Luau; la paridad byte a byte con una versión concreta de Roblox debe fijarse mediante vectores dorados obtenidos de esa versión.

## Tests

```bash
make test
# equivalente:
ctest --test-dir build --output-on-failure
```

Las suites cubren argumentos y stdout, `--json`, timeout, sintaxis moderna, biblioteca estándar, jerarquía y reparenting de instancias, eventos de alta/baja de hijos, atributos, enumeraciones, tipos compatibles con las comprobaciones de Fengetheus, destrucción recursiva, llamadas con `:` y scheduling/cancelación básica.

Para inspeccionar la salida estructurada:

```bash
./bin/lumora --json tests/roblox_api.lua
```

## Relación con Fengetheus

Fengetheus genera archivos `.lua` o `.luau` y puede seleccionar Luau como destino. Lumora es el paso de ejecución posterior: recibe el archivo directamente, aporta el entorno Roblox mínimo esperado por código ofuscado y devuelve un resultado reproducible para tests o pipelines de validación.

## Compatibilidad y alcance

El compilador y la VM son Luau oficial. La capa Roblox es una emulación headless orientada a ejecutar y validar output, no un reemplazo de Roblox Studio ni del motor 3D. Los mensajes de error nativos de Luau se conservan; los mensajes de APIs Roblox emuladas se aproximan y se mantienen cubiertos por regresiones donde resulta práctico.

## Licencia

El código de Lumora mantiene la licencia del repositorio. Luau vendorizado conserva sus avisos y licencia originales en `vendor/luau`.
