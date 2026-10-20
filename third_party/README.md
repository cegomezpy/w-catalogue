# Dependencias de terceros

Todas están vendorizadas con versión fija; no se descarga nada al compilar.

| Biblioteca | Versión | Licencia | Ubicación | Uso |
|---|---|---|---|---|
| cpp-httplib | 0.61.0 | MIT (`httplib/LICENSE`) | `httplib/httplib.h` | Servidor HTTP local |
| nlohmann/json | 3.12.0 | MIT (`json/LICENSE`) | `json/nlohmann/json.hpp` | JSON |
| SQLite (amalgamation) | 3.54.1 | Dominio público | `sqlite/sqlite3.c`, `sqlite/sqlite3.h` | Base de datos |
| stb_truetype | 1.26 | Dominio público / MIT (en el encabezado del archivo) | `stb/stb_truetype.h` | Lectura de metadatos de fuentes |
| stb_image | 2.30 | Dominio público / MIT (en el encabezado del archivo) | `stb/stb_image.h` | Decodificación de JPEG y PNG |
| stb_image_resize2 | 2.18 | Dominio público / MIT (en el encabezado del archivo) | `stb/stb_image_resize2.h` | Redimensionado de fotos (ver nota) |
| stb_image_write | 1.16 | Dominio público / MIT (en el encabezado del archivo) | `stb/stb_image_write.h` | Codificación de JPEG |
| Inja | 3.5.0 | MIT (`inja/LICENSE`) | `inja/inja.hpp` | Plantillas de los documentos (single_include) |
| doctest | 2.5.3 | MIT (`doctest/LICENSE`) | `doctest/doctest.h` | Tests (solo desarrollo) |
| Alpine.js | 3.15.0 | MIT (`web/vendor/alpine.LICENSE`) | `web/vendor/alpine.min.js` | Interfaz web |
| SortableJS | 1.15.6 | MIT (`web/vendor/sortable.LICENSE`) | `web/vendor/Sortable.min.js` | Reordenar fotos arrastrando |

SQLite no incluye archivo de licencia: su código fuente es de dominio público
(<https://www.sqlite.org/copyright.html>).

`stb_image_resize2.h` ya no está en la raíz del repositorio de stb: la copia vendorizada procede de
`deprecated/stb_image_resize2.h` (v2.18), que es completa y funcional; su cabecera indica que las versiones
nuevas viven en <https://github.com/nothings/stb_image_resize>. Las implementaciones de las tres
bibliotecas de imagen se compilan en una sola unidad: `src/imaging/stb_image_impl.cpp`.

## Actualizar una dependencia

Reemplazar el archivo, actualizar la versión en esta tabla y ejecutar `ctest`.

## Parches locales

### cpp-httplib 0.61.0: `thread_local` -> `static`

Seis objetos con destructor no trivial (`builtin_methods`, `prohibited_trailers`, y las expresiones
regulares de `file_extension`, de la línea de estado HTTP y de `append_query_params`) se declaran
`thread_local` en el original. Con GCC/MinGW-w64 (TLS emulado) y winpthreads enlazado estáticamente, el
destructor se ejecuta a veces sobre memoria ya liberada cuando un hilo del servidor termina, y el proceso
se cae (SIGSEGV, ~8 % de las ejecuciones de la batería de pruebas; 0 de 40 con el parche). Se cambiaron
a `static`, que es seguro para uso de solo lectura desde varios hilos.

Al actualizar cpp-httplib hay que reaplicar el cambio (buscar `thread_local` en `httplib.h`; los de
`std::mt19937` y los de OpenSSL no hacen falta) y volver a ejecutar las pruebas varias veces.
