# Tasks

## 1. Estructura y build

- [x] 1.1 Crear la estructura de carpetas (`src/`, `web/`, `third_party/`, `resources/`, `tests/`) y un `.gitignore` que excluya `build/`, `dist/` y `data/`; verificar que las carpetas existen
- [x] 1.2 Vendorizar cpp-httplib, nlohmann/json, SQLite amalgamation y doctest en `third_party/` con versiones fijas, y documentar versión y licencia en `third_party/README.md`; verificar que los archivos están y que las licencias aparecen listadas
- [x] 1.3 Escribir `CMakeLists.txt` con C++17, la biblioteca `catalog_core`, el exe `WCatalog` (subsistema GUI, enlace estático) y el header de versión generado desde `project(VERSION)`; verificar que `cmake -G "MinGW Makefiles" -B build && cmake --build build` produce `WCatalog.exe`
- [x] 1.4 Añadir `resources/app.manifest` (activeCodePage UTF-8) y `app.rc` con icono, compilados con windres; verificar con `objdump -p build/WCatalog.exe` que no depende de `libstdc++`/`libgcc`/`libwinpthread` y que el manifest está incluido
- [x] 1.5 Añadir el target `catalog_tests` con doctest registrado en ctest; verificar que `ctest --test-dir build` ejecuta una prueba trivial con éxito

## 2. Rutas, carpeta de datos y log

- [x] 2.1 Implementar la resolución de la carpeta del exe y de `data/` (con `logs/` y `backups/`), creándolas si faltan; verificar con tests que se crean en un directorio temporal, incluida una ruta con tildes y ñ
- [x] 2.2 Implementar el logger a `data/logs/app.log` con rotación al superar 1 MB; verificar con un test que escribe, rota y conserva el archivo anterior
- [x] 2.3 Implementar el diálogo de error fatal (`MessageBoxW`) y su uso cuando `data/` no es escribible; verificar manualmente ejecutando desde una carpeta de solo lectura

## 3. Base de datos y migraciones

- [x] 3.1 Implementar el wrapper de SQLite (apertura, `WAL`, `foreign_keys`, mutex, sentencias preparadas y transacciones); verificar con tests sobre una base temporal
- [x] 3.2 Implementar el ejecutor de migraciones con `user_version`, una transacción por migración y la migración 1 (`app_meta`); verificar con tests: base nueva, base ya migrada y migración fallida con rollback
- [x] 3.3 Implementar la copia de seguridad antes de migrar (con retención de 5) y el rechazo de una base con versión mayor; verificar con tests que se crea el backup y que una base "del futuro" no se modifica

## 4. Servidor HTTP y API base

- [x] 4.1 Implementar el servidor en `127.0.0.1` con puerto libre y validación del encabezado `Host`; verificar con un test HTTP que un `Host` ajeno recibe 403
- [x] 4.2 Implementar los helpers de respuesta y error JSON, el 404 JSON para `/api/*` y el manejador global de excepciones; verificar con tests de ruta inexistente y de excepción forzada (500 JSON, el servidor sigue respondiendo)
- [x] 4.3 Implementar `GET /api/health` y `GET /api/info`; verificar con un test que `info` devuelve `version`, `dataDir` y `schemaVersion`, y que el texto UTF-8 llega intacto
- [x] 4.4 Servir los archivos estáticos de `web/` con sus tipos MIME y 404 para lo que no existe; verificar con un test que `/` devuelve `index.html` y que un `.css` llega con `text/css`

## 5. Ventana y ciclo de vida

- [x] 5.1 Implementar la localización de Edge (registro y rutas conocidas) y su lanzamiento con `--app` y perfil en `%LOCALAPPDATA%\WCatalog\edge-profile`; verificar manualmente que se abre una ventana sin barras de navegador
- [x] 5.2 Implementar el respaldo con el navegador predeterminado cuando no se encuentra Edge; verificar forzando una ruta de Edge inválida (variable de entorno de depuración) y comprobando que se abre el navegador predeterminado
- [x] 5.3 Implementar el canal de presencia SSE `/api/session/events` con keep-alive, el periodo de gracia de 10 s y el apagado ordenado (cerrando la base); verificar con un test que el servidor se detiene tras cerrar la última conexión y que no se detiene si hay reconexión dentro de la gracia
- [x] 5.4 Implementar la instancia única (mutex y `data/.instance`) y abrir una ventana hacia la instancia existente en el segundo arranque; verificar manualmente con un doble arranque que hay un solo proceso `WCatalog.exe`

## 6. Shell de la interfaz

- [x] 6.1 Crear `web/index.html` con el layout base (barra lateral de secciones y área de contenido), `css/app.css` y Alpine.js vendorizado; verificar que carga sin errores en la consola del navegador
- [x] 6.2 Implementar el router por hash, el cliente `api.js` (fetch con manejo del error común y notificaciones) y la conexión al canal de presencia; verificar que navegar entre secciones cambia el contenido y que un error de API se muestra como notificación
- [x] 6.3 Crear la página de inicio con los datos de `/api/info` (versión, carpeta de datos y versión del esquema); verificar visualmente que los valores coinciden con la respuesta de la API

## 7. Distribución y documentación

- [x] 7.1 Configurar `cmake --install` para generar `dist/WCatalog/` con el exe, `web/` y los recursos; verificar que la carpeta contiene solo lo necesario
- [x] 7.2 Escribir el `README.md` con requisitos, compilación, ejecución, distribución y la nota sobre SmartScreen; verificar que los comandos documentados funcionan tal como están escritos

## 8. Verificación integrada

- [x] 8.1 Prueba de punta a punta: copiar `dist/WCatalog/` a otra ruta (con espacios y tildes), ejecutar y comprobar que se crea `data/`, que la ventana muestra la información y que al cerrarla el proceso termina en menos de 15 s
