# Proposal

## Why

El proyecto está vacío y todas las funcionalidades previstas (ajustes, casas, fotos, exportación de flyers y catálogos) necesitan una base ejecutable común. La prioridad es tener pronto una aplicación local para Windows 11 que se copie a otra máquina y funcione sin instalador. Por eso primero hay que fijar la arquitectura: un ejecutable C++, un servidor local, una interfaz HTML/CSS/JS dentro de una ventana de Edge y almacenamiento SQLite portable.

## What Changes

- Proyecto C++17 compilado con CMake y GCC (MinGW-w64), enlazado de forma estática para producir un único `WCatalog.exe` sin DLLs adicionales.
- Servidor HTTP local, accesible solo desde `127.0.0.1` y en un puerto libre, que sirve la interfaz web y una API JSON bajo `/api`.
- Ventana de la aplicación con Microsoft Edge en modo aplicación (sin barras de navegador). Si Edge no está disponible, se usa el navegador predeterminado.
- Ciclo de vida: al cerrar la ventana, el proceso termina. Solo puede haber una instancia por carpeta de datos.
- Modo portable: la carpeta `data/` junto al ejecutable guarda la base de datos, los logs y, más adelante, fotos, fuentes y salidas. Se crea en el primer arranque.
- Base de datos SQLite con migraciones versionadas automáticas y copia de seguridad antes de migrar. En este change no hay tablas de negocio, solo la infraestructura.
- Shell de la interfaz: HTML/CSS/JS sin paso de compilación, con navegación entre secciones y una página de inicio que muestra el estado de la app.
- Endpoints `/api/health` y `/api/info`, y formato común de respuestas y errores JSON.
- Registro de errores en archivo y aviso visible de los errores fatales de arranque.
- Objetivo de distribución que genera una carpeta lista para copiar a otra máquina.

## Capabilities

### New Capabilities
- `app-runtime`: arranque, ventana de la aplicación, ciclo de vida, instancia única, carpeta de datos portable, base de datos con migraciones y manejo de errores fatales.
- `local-api`: servidor HTTP local, restricción de acceso a la máquina, convenciones de respuestas y errores JSON, endpoints de salud e información, y entrega de la interfaz web.

### Modified Capabilities
<!-- Ninguna: no existen specs previas. -->

## Impact

- **Código nuevo:** `CMakeLists.txt`, `src/` (runtime, http, db, util), `web/` (shell de la interfaz), `tests/`, `resources/` (manifest e icono).
- **Dependencias vendorizadas** en `third_party/`: cpp-httplib (MIT), nlohmann/json (MIT), SQLite amalgamation (dominio público), doctest (MIT, solo tests), Alpine.js (MIT, frontend).
- **Requisitos de la máquina destino:** Windows 11 x64 con Microsoft Edge, que viene preinstalado. No requiere instalador ni runtimes adicionales.
- **Base para:** `add-catalog-settings`, `add-house-records`, `add-house-photos`, `add-document-export`.
