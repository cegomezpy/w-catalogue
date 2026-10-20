# WCatalog

Aplicación local para Windows 11 que genera flyers y catálogos de casas. Es un único
ejecutable (`WCatalog.exe`) que lanza un servidor local y abre la interfaz en una ventana
de Microsoft Edge sin barras de navegador. Es portable: no necesita instalador y guarda
todo en la carpeta `data/` que está junto al ejecutable.

## Requisitos

**Para ejecutar** (máquina destino): Windows 11 x64 con Microsoft Edge (viene incluido).

**Para compilar:**

- GCC 13 o superior (MinGW-w64, UCRT) en el `PATH`
- CMake 3.20 o superior
- `mingw32-make`

Las dependencias (cpp-httplib, nlohmann/json, SQLite, doctest, Alpine.js) ya están en
`third_party/` y `web/vendor/`; ver `third_party/README.md`.

## Compilar

```
cmake -G "MinGW Makefiles" -B build
cmake --build build
```

Esto produce `build\WCatalog.exe` (Release, enlace estático, sin DLLs de MinGW).
Durante la compilación el enlazador puede mostrar el aviso
`multiple non-default manifests`; es inofensivo: el manifest de la aplicación (UTF-8)
es el que se aplica.

## Pruebas

```
ctest --test-dir build --output-on-failure
```

## Ejecutar

```
build\WCatalog.exe
```

El ejecutable compilado en `build\` usa la carpeta `web\` del repositorio, así que los
cambios de interfaz (HTML/CSS/JS) se ven recargando la ventana (F5), sin recompilar.
Los datos se guardan en `build\data\`.

Al cerrar la ventana, el proceso termina en unos 10 segundos. Si Edge no se encuentra,
la interfaz se abre en el navegador predeterminado.

Variable de depuración: `WCATALOG_EDGE_PATH` fuerza la ruta de `msedge.exe`; si apunta a
un archivo inexistente se simula "Edge no instalado".

## Ajustes y fuentes

En **Ajustes** (menú lateral) se configuran los tres colores del tema, la fuente de títulos y la de texto, los datos de contacto y la moneda por defecto. Pulsa **Guardar ajustes** para aplicar los cambios; la vista previa se actualiza al guardar.

### Fuentes incluidas

Vienen con la aplicación en la carpeta `fonts/` (pesos 400 y 700, licencia SIL Open Font License; el texto de cada licencia está en `fonts/licencias/`):

| Familia | Licencia |
|---|---|
| Inter | OFL 1.1 |
| Montserrat | OFL 1.1 |
| Poppins | OFL 1.1 |
| Playfair Display | OFL 1.1 |

No se pueden eliminar.

### Importar una fuente

1. En Ajustes → **Tipografía**, pulsa **Buscar fuentes en Google Fonts**. Se abre <https://fonts.google.com> en tu navegador predeterminado.
2. Descarga la familia que quieras y descomprime el ZIP. Usa los archivos `.ttf` u `.otf` de la carpeta `static` (por ejemplo `Lato-Regular.ttf` y `Lato-Bold.ttf`); evita los archivos de fuente variable (con corchetes en el nombre).
3. En **Importar fuente** elige un archivo (hasta 10 MB). La aplicación lee del archivo la familia, el peso y el estilo; puedes corregirlos antes de confirmar con **Importar**.
4. Repite con cada variante. Las variantes con la misma familia se agrupan bajo un solo nombre.
5. Elige la nueva familia en **Fuente de títulos** o **Fuente de texto** y pulsa **Guardar ajustes**.

Si importas una variante que ya existe, la aplicación pregunta si quieres reemplazarla. Para eliminar una fuente importada, usa **Eliminar** junto a su variante; si estaba elegida en los ajustes, vuelve a la fuente por defecto y se muestra un aviso. Algunas fuentes restringen su incrustación en PDF en la licencia; la aplicación lo avisa al importar y la responsabilidad de la licencia es tuya.

Las fuentes importadas se guardan en `data/fuentes/`, así que viajan con la carpeta de datos.

## Distribuir

```
cmake --install build --prefix dist/WCatalog
```

Genera `dist\WCatalog\` con `WCatalog.exe`, `web\` y `fonts\`. Se copia esa carpeta completa a
cualquier ubicación o máquina (USB, red local) y se ejecuta con doble clic. La carpeta
`data\` se crea sola en el primer arranque; para llevarse los datos, copiar también `data\`.

### Aviso de SmartScreen

El ejecutable no está firmado. Si Windows muestra "Windows protegió su PC", pulsar
**Más información → Ejecutar de todas formas**. Copiar la carpeta por USB o red local no
añade la marca de "descargado de internet", así que normalmente el aviso no aparece. Algunos
antivirus pueden marcar ejecutables sin firmar; hay que permitir la carpeta si ocurre.

## Estructura

```
src/         código C++ (runtime, http, db)
web/         interfaz HTML/CSS/JS sin paso de compilación (Alpine.js)
third_party/ dependencias vendorizadas
resources/   manifest, icono y recursos de Windows
tests/       pruebas doctest
```

Los datos del usuario viven en `data/` (base `catalog.db`, `logs/`, `backups/`). El perfil
de la ventana de Edge está aparte, en `%LOCALAPPDATA%\WCatalog\edge-profile`, y solo contiene
caché y preferencias de la ventana.
