# Lumora

Lumora es un runtime autocontenido de **Luau** para ejecutar y validar el output generado por Fengetheus fuera de Roblox Studio. El objetivo es ofrecer una superficie headless, determinista y comprobable para scripts ofuscados y casos de anti-tamper.

## Requisitos y build

En Debian/Ubuntu se necesita un compilador C++17, CMake y Ninja. Luau está incluido en `vendor/luau`, por lo que no se requiere instalar una librería Luau del sistema ni usar un gestor de paquetes adicional.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
```

El ejecutable queda disponible como `build/bin/luau-vm` y, después del build, como `bin/luau-vm`.

## Uso

```bash
./bin/luau-vm script.lua argumento1 argumento2
./bin/luau-vm --no-roblox script.lua
./bin/luau-vm --json script.lua argumento1
./bin/luau-vm --timeout 5 script.lua
```

Los argumentos se exponen con la convención habitual de Lua: `arg[0]` contiene la ruta del script y `arg[1]` en adelante contienen los argumentos del usuario. `--no-roblox` omite el prelude de emulación para probar Luau puro. `--json` captura stdout, stderr, error y código de salida en un único objeto. `--timeout` interrumpe loops largos mediante la interrupción cooperativa de Luau y el proceso JSON tiene además una barrera de seguridad.

## API emulada

| Área | Superficie disponible |
| --- | --- |
| Árbol Roblox | `game`, `workspace`, `Instance.new`, `GetService`, `GetChildren`, `FindFirstChild`, `WaitForChild`, `GetFullName`, `Destroy`, `IsA` |
| Eventos | `Connect`, `Disconnect`, `Connected`, `Fire`, `AttributeChanged`, `ChildAdded` |
| Atributos | `GetAttribute`, `SetAttribute` |
| Enumeraciones | `Enum.X.Y`, `Name`, `EnumType`, `Value` |
| Tipos | `typeof`, `Vector2.new`, `UDim2.new`, `UDim2.fromScale`, `UDim2.fromOffset`, `Path2D.new` |
| Scheduling | `task.spawn`, `task.delay`, `task.cancel`, `task.wait` |
| Funciones | `iscclosure`, `islclosure`, `newcclosure`, `clonefunction`, `getfenv`, `setfenv` |
| Random | `Random.new(seed)`, `NextInteger`, `NextNumber`, `NextUnitVector` |
| Biblioteca | Biblioteca estándar Luau, `bit32`, `string.pack/unpack`, `buffer`, `utf8` y tipos modernos del compilador Luau |

La implementación se encuentra deliberadamente en capas: la VM y el compilador son Luau oficial, mientras que la API Roblox se registra en un prelude aislado para que los casos de Fengetheus sean fáciles de extender y probar.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Los tests actuales verifican ejecución con argumentos y stdout, además de creación de instancias, jerarquía, eventos, atributos, enumeraciones, tipos y Random. Para probar manualmente la salida estructurada:

```bash
./bin/luau-vm --json tests/roblox_api.lua
```

## Relación con Fengetheus

Fengetheus genera archivos `.lua` o `.luau` y puede seleccionar Luau como destino. Lumora está pensado como el paso de ejecución posterior: recibe ese archivo directamente, aporta el entorno Roblox mínimo esperado por código ofuscado y devuelve un resultado reproducible para tests o pipelines de validación.

## Estado de compatibilidad

La compatibilidad se amplía de forma incremental mediante tests de regresión. En particular, nombres y mensajes exactos de errores propios de Roblox requieren implementar cada familia de userdata y metamétodos de forma nativa; el prelude actual cubre la ruta funcional principal y deja esos puntos aislados para endurecimiento posterior.

## Licencia

El código de Lumora mantiene la licencia del repositorio. Luau vendorizado conserva sus avisos y licencia originales en `vendor/luau`.
