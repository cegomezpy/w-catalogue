# Design

## Context

El repositorio no tiene código. La máquina de desarrollo tiene GCC 16.2 (MinGW-w64, UCRT) y CMake 4.4, pero no MSVC. Las máquinas destino tienen Windows 11 y, por tanto, Edge. En un spike previo se comprobó que `msedge --headless --print-to-pdf` convierte HTML/CSS en un PDF con tamaño de página personalizado, fuentes incrustadas y texto vectorial. Ese resultado sostiene la decisión de usar HTML/CSS como motor de render (lo usa `add-document-export`).

Este change define la arquitectura que todos los demás reutilizan:

```
+----------------------------- WCatalog.exe (C++17) ----------------------+
|  runtime: rutas, instancia única, lanzador de Edge, ciclo de vida, log  |
|  http:    servidor 127.0.0.1:<puerto>  ->  /api/*  (JSON)              |
|                                        ->  /*      (web/ estático)      |
|  db:      SQLite + migraciones                                          |
+-------------------------------------------------------------------------+
        ^ ventana: msedge --app=http://127.0.0.1:<puerto>/
        |
   web/ (HTML/CSS/JS, Alpine.js)            data/ (catalog.db, logs, backups...)
```

## Goals / Non-Goals

**Goals:**
- Que el ejecutable se compile con un solo comando y se distribuya copiando una carpeta.
- Una estructura de código en capas (http → servicios → repositorios) que los siguientes changes puedan ampliar sin rehacerla.
- Que las tareas de interfaz se puedan iterar sin recompilar.

**Non-Goals:**
- Tablas de negocio (casas, ajustes, documentos): llegan en los changes siguientes.
- Ventana nativa propia (WebView2), instalador, auto-actualización, firma de código.
- Diseño visual de la interfaz más allá de un estilo base limpio.

## Decisions

### D1. Toolchain: GCC MinGW-w64 + CMake, enlace estático
Se usan `-static`, `-static-libgcc` y `-static-libstdc++`, y el subsistema GUI (`-mwindows`), para obtener un único exe sin consola ni DLLs de MinGW.
- *Alternativa:* MSVC. Requiere instalar Build Tools y no aporta nada mientras no se use WebView2.
- Se incluye un manifest con `activeCodePage = UTF-8`. Así las APIs "narrow" de Windows trabajan en UTF-8 y las rutas con tildes o ñ funcionan sin convertir a wide en cada llamada. Las rutas internas se manejan con `std::filesystem::path`.

### D2. Servidor HTTP: cpp-httplib
Es header-only, tiene licencia MIT, soporta multipart (necesario para subir fotos y fuentes) y streaming, y compila con MinGW.
- *Alternativas:* mongoose (licencia GPL/comercial), Crow o Drogon (más pesados y con más dependencias), civetweb (API en C, más verbosa).
- Escucha en `127.0.0.1` con `bind_to_any_port`, es decir, en un puerto libre que asigna el sistema. Un middleware previo valida el encabezado `Host` frente a `127.0.0.1:<puerto>` y `localhost:<puerto>` para mitigar DNS rebinding.
- Un helper común serializa las respuestas JSON y los errores con la forma definida en el spec `local-api`. Un manejador de excepciones global convierte cualquier excepción en un 500 JSON.

### D3. JSON: nlohmann/json
Es la opción estándar de facto en C++ y permite convertir directamente entre structs y JSON. Inja, que usarán las plantillas de exportación, también depende de ella.

### D4. Base de datos: SQLite amalgamation + migraciones en código
- Una sola conexión protegida por mutex, con `journal_mode=WAL` y `foreign_keys=ON`. El volumen de datos es pequeño y un solo usuario no justifica un pool.
- Las migraciones son un arreglo ordenado de scripts SQL dentro del código, y `PRAGMA user_version` guarda la versión. Cada migración corre en su propia transacción, así que si falla se revierte (rollback) y se informa.
- Antes de migrar una base existente se hace una copia con `sqlite3_backup` en `data/backups/catalog-v<N>-<fecha>.db`, y se conservan las 5 más recientes.
- Si `user_version` es mayor que la versión que conoce la app, se aborta sin escribir nada.
- La migración 1 de este change crea solo `app_meta(clave, valor)`. Cada change siguiente añade la siguiente migración disponible.

### D5. Ventana: Edge en modo `--app` con perfil propio
- Para localizar `msedge.exe`, primero se consulta el registro (`App Paths\msedge.exe`) y después las rutas conocidas de `Program Files (x86)` y `Program Files`.
- Se lanza con `--app=<url> --user-data-dir=%LOCALAPPDATA%\WCatalog\edge-profile --no-first-run --no-default-browser-check`. El perfil propio aísla la ventana de la sesión normal de Edge del usuario. Se guarda en `%LOCALAPPDATA%` y no en `data/` para no copiar decenas de MB de caché de navegador junto con los datos.
- Si Edge no se encuentra, se abre la URL con `ShellExecuteW` en el navegador predeterminado.
- *Alternativa:* WebView2. Daría una ventana propia, pero su SDK oficial se apoya en WRL/MSVC. Se puede revisar más adelante.

### D6. Detección de cierre: canal de presencia (SSE)
La interfaz abre una conexión Server-Sent Events con `/api/session/events`, y el servidor envía un keep-alive cada 5 s. Cuando el número de conexiones activas llega a 0, el servidor espera un periodo de gracia de 10 s (para tolerar recargas) y, si nadie se reconecta, se cierra de forma ordenada.
- Funciona igual con Edge que con el navegador de respaldo.
- *Alternativas descartadas:*
  - Esperar el handle del proceso de Edge. Edge puede delegar en otro proceso o mantenerse en segundo plano, así que no es fiable como mecanismo único. Se usa como acelerador: si el proceso termina, se aplica el mismo periodo de gracia.
  - Un heartbeat por temporizador. Edge reduce los temporizadores de las pestañas ocultas o minimizadas, lo que provocaría cierres falsos.

### D7. Instancia única
Un mutex con nombre `Local\WCatalog-<hash de la ruta de data/>` permite que dos copias en carpetas distintas convivan. La instancia activa escribe su puerto en `data/.instance`. Un segundo arranque detecta el mutex, lee el puerto, abre una ventana nueva hacia esa URL y termina.

### D8. Frontend sin build
- `web/` vive junto al exe y no va embebido, para que los cambios de interfaz no requieran recompilar.
- Alpine.js va vendorizado en `web/vendor/`, sin CDN, para que todo funcione sin conexión.
- Navegación por hash (`#/inicio`, `#/casas`, ...) con un router mínimo propio y un `fetch` envolvente que interpreta el formato de error común.
- *Alternativa:* React/Vite. Añadiría Node y un paso de compilación, y la interfaz es de formularios, así que no lo justifica.

### D9. Convenciones de nombres
- Los términos del dominio van en español en la base de datos, la API y el JSON (`casa`, `/api/casas`, `comedores_independientes`), porque coinciden con el vocabulario del usuario.
- El código C++ (clases, funciones) va en inglés.
- La capa de servicios valida y la de repositorios solo persiste.

### D10. Estructura del repositorio
```
CMakeLists.txt
src/
  main.cpp
  runtime/   paths, instance, edge_launcher, lifecycle, log
  http/      server, json_response, routes_core
  db/        database, migrations
third_party/ httplib/ json/ sqlite/ doctest/   (+ LICENSES y versiones en README)
web/         index.html, css/app.css, js/app.js, js/router.js, js/api.js, vendor/
resources/   app.rc, app.manifest, app.ico
tests/       pruebas doctest -> catalog_tests
```
El núcleo se compila como biblioteca estática `catalog_core`, que enlazan tanto el exe como los tests.

### D11. Tests y distribución
- Se usan doctest y `ctest`, y los tests de integración HTTP usan el cliente de cpp-httplib contra un servidor en puerto efímero.
- La distribución se genera con `cmake --install build --prefix dist/WCatalog`, que copia el exe, `web/` y los recursos. `data/` no se distribuye, se crea en el primer arranque.
- La versión de la app sale de `project(VERSION)` mediante un header generado.

## Risks / Trade-offs

- **[Edge cambia sus flags o su comportamiento de proceso]** → La detección de cierre no depende del proceso de Edge (D6). El lanzador está aislado en un módulo.
- **[Antivirus o SmartScreen marcan un exe sin firmar]** → Se documenta en el README. Copiar por USB o red local no añade la marca de "descargado de internet". La firma de código queda fuera de alcance.
- **[El puerto cambia en cada arranque]** → Es intencional. La URL nunca se guarda fuera de `data/.instance`.
- **[El perfil de Edge en `%LOCALAPPDATA%` no viaja con la carpeta]** → Solo contiene caché y preferencias de la ventana, ningún dato del usuario.
- **[`web/` editable por el usuario]** → Es aceptable en una app local de un solo usuario y facilita iterar.

## Migration Plan

No aplica: es la primera versión y no hay datos previos.
