# Comparación y adaptación de capacidades

## Estado de los repositorios revisados

Se comparó Lumora con los repositorios públicos de Lune y Lute. Lumora mantiene su runtime Luau embebido, prelude Roblox headless, filesystem virtual, scheduler cooperativo, JSON, regex, análisis estático, CLI de inspección/deobfuscación y laboratorio visual. La integración nueva no copia el árbol ni el sistema de build de esos proyectos.

## APIs incorporadas

| Módulo | Capacidades |
| --- | --- |
| `@lumora/stringext` | Prefijos, sufijos, trim y conteo de patrones. |
| `@lumora/tableext` | Mapeo, filtrado, búsqueda, combinación, claves, valores, clonado profundo, sets, extensión, reversa, any/all y concatenación. |
| `@lumora/time` | Duraciones normalizadas desde nanosegundos hasta semanas, suma, resta y conversión a segundos. |
| `@lumora/system` | OS, arquitectura, hostname, directorio temporal, CPU, hilos, uptime y memoria. |
| `@lumora/crypto` | `digest`, `hash`, `hmac`, `randomBytes` y `base64.encode/decode`, implementados con OpenSSL. |
| `@lumora/net` | `request`, `get`, sockets TCP, cliente WebSocket `ws://`, servidor HTTP y servidor WebSocket RFC6455. |
| `@lumora/syntax` | Parser Luau con errores estructurados, AST visitado, datos CST, comentarios, posiciones, `nodeList`, conteos y fuente original. |
| `@lumora/vm` | Estados Luau aislados, ejecución de fuentes, retorno de valores primitivos, cierre explícito y recolección segura. |
| `@lumora/debugger` | Sesiones persistentes, breakpoints por línea, continuación, stepping instrucción a instrucción, línea actual, hits, estado y errores. Usa los callbacks públicos de depuración de Luau. |

## Red

El cliente HTTP usa libcurl con métodos, cuerpo, cabeceras, redirecciones y timeout de 30 segundos. El resultado contiene `status`, `body` y `ok`. El servidor HTTP es síncrono y procesa una conexión por llamada, lo que hace su comportamiento determinista y adecuado para scripts controlados. Los WebSockets implementan handshake RFC6455, frames de texto pequeños, enmascaramiento de cliente, desemmascaramiento de servidor y cierre mediante userdata.

La implementación WebSocket actual soporta `ws://` y no `wss://`; TLS para WebSocket debe añadirse explícitamente con una política de certificados. Los límites de frame son 64 KiB y el servidor síncrono acepta una conexión por llamada. HTTP utiliza libcurl y hereda los protocolos habilitados por la instalación del sistema.

## Criptografía

Las primitivas usan OpenSSL EVP/HMAC/RAND y trabajan con strings binarios de Luau. `digest` y `hmac` aceptan nombres OpenSSL como `sha256` y `sha512`; `randomBytes` limita solicitudes individuales a 1 MiB; Base64 es binario-safe. No se inventan algoritmos criptográficos en Luau ni se presenta un hash no criptográfico como seguridad.

## Parser/CST

`@lumora/syntax.parse(source)` activa `captureComments` y `storeCstData` del parser oficial vendorizado de Luau. Devuelve la fuente, líneas, errores con mensaje y línea, conteos de nodos/expresiones/sentencias, número de nodos CST y una lista de posiciones de nodos. El árbol se visita dentro de la vida útil del allocator y no se expone ningún puntero inválido al script.

## VMs aisladas y debugger

Cada VM se crea con un `lua_State` independiente y bibliotecas Luau propias. `vm:eval(source)` conserva resultados numéricos, booleanos y strings; los errores se devuelven como segundo resultado (`nil, error`). El cierre es idempotente y también se ejecuta mediante `__gc`.

El debugger compila con nivel de depuración alto, instala callbacks `debugbreak` y `debugstep`, programa breakpoints sobre la función cargada y usa `lua_break`/`lua_singlestep` para detener y continuar. `start` devuelve `false` cuando la primera ejecución se detiene en breakpoint y `true` cuando termina sin detenerse; `continue` y `step` devuelven una tabla con `done`, `line`, `hits` y, si corresponde, `error`.

## Dependencias

El único código nuevo externo enlazado son las interfaces públicas del sistema: libcurl, OpenSSL, libsodium y libuv. La implementación actual utiliza libcurl y OpenSSL directamente; las otras bibliotecas quedan disponibles para futuras ampliaciones sin hacer que el runtime dependa del código o namespaces de los repositorios comparados.

No se incorporaron rutas `@lune/*` ni `@lute/*`, archivos de build, submódulos, namespaces, resolvers ni referencias operativas a esos proyectos.

## Validación ejecutada

Se ejecutaron correctamente:

- Compilación CMake completa con `g++`.
- `tests/native_modules_contract.lua`.
- `tests/native_network_contract.sh`, con HTTP y WebSocket contra fixtures locales.
- `tests/native_http_server_contract.sh`, con cliente TCP externo contra el servidor HTTP de Lumora.
- `tests/stdlib_contract.lua`.
- Suite ejecutable existente de Lumora: analyzer, CLI, JSON, sandbox, smoke y contratos relacionados.

El diferencial contra el CLI oficial de Luau solo se omite cuando el binario oficial no está instalado en el entorno.
