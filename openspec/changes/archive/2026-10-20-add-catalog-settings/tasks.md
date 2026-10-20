# Tasks

## 1. Datos y dependencias

- [x] 1.1 Vendorizar `stb_truetype.h` en `third_party/` y registrar su versión y licencia en `third_party/README.md`; verificar que compila dentro de `catalog_core`
- [x] 1.2 Añadir la migración que crea las tablas `ajustes` y `fuente`; verificar con un test que la base pasa a la nueva versión y conserva `app_meta`
- [x] 1.3 Añadir `fonts/` con Inter, Montserrat, Poppins y Playfair Display (TTF estáticas, pesos 400 y 700), sus licencias OFL y `fuentes.json`, y configurar `cmake --install` para copiarlos; verificar que `dist/WCatalog/fonts/` contiene los archivos

## 2. Servicio de ajustes

- [x] 2.1 Implementar el servicio de ajustes (esquema de claves, valores por defecto y validación de hex, email y moneda) y su repositorio; verificar con tests de valores por defecto, color inválido, email inválido y guardado parcial de contacto
- [x] 2.2 Implementar `GET /api/ajustes` y `PUT /api/ajustes` (transaccional, con errores 400 por campo); verificar con tests HTTP que un PUT inválido no modifica los valores guardados

## 3. Biblioteca de fuentes

- [x] 3.1 Implementar la carga del manifiesto de fuentes incluidas y la vista unificada por familias (incluidas e importadas); verificar con un test que la lista contiene las 4 familias incluidas
- [x] 3.2 Implementar la lectura de metadatos (tablas `name` y `OS/2`) y la validación de archivos; verificar con tests usando una TTF incluida (familia, peso y estilo correctos) y un archivo no válido (rechazado)
- [x] 3.3 Implementar `POST /api/fuentes/analizar` y `POST /api/fuentes` (token, reemplazo y 409 por duplicado), `DELETE /api/fuentes/{id}` (borra el archivo y restablece el ajuste en uso con `avisos`), y la limpieza de `data/tmp/`; verificar con tests HTTP de importación, duplicado, reemplazo y eliminación de una fuente en uso
- [x] 3.4 Servir `/media/fuentes/incluidas/*` y `/media/fuentes/importadas/*` con tipo `font/ttf`/`font/otf`; verificar con un test que una fuente incluida se descarga correctamente

## 4. Tema CSS y enlaces externos

- [x] 4.1 Implementar la generación del CSS del tema con URL base parametrizable y `GET /api/tema.css` (`no-store`); verificar con tests que contiene las 5 variables y los `@font-face` de las familias elegidas, y que refleja un cambio de color inmediatamente
- [x] 4.2 Implementar `POST /api/sistema/abrir-enlace` restringido a `https://`; verificar con un test que `file:` y `http:` devuelven 400, y manualmente que el botón abre Google Fonts

## 5. Interfaz de Ajustes

- [x] 5.1 Crear la página `#/ajustes` con las secciones Tema (selectores de color sincronizados con hex y tarjeta de vista previa), Contacto y General (moneda); verificar manualmente que guardar actualiza la vista previa y que los errores de validación se muestran junto al campo
- [x] 5.2 Crear la sección Tipografía: selectores de títulos y texto, lista de familias con texto de muestra ("Casa amplia con jardín — Peña, ñandú, acción"), importación en dos pasos con edición de metadatos y confirmación de reemplazo, eliminación con confirmación y botón de Google Fonts; verificar manualmente importando una fuente descargada y viéndola en la vista previa sin recargar
- [x] 5.3 Documentar en el `README.md` cómo importar fuentes y las fuentes incluidas con sus licencias; verificar que los pasos descritos funcionan tal como están escritos

## 6. Verificación integrada

- [x] 6.1 De punta a punta: importar una fuente, elegirla para títulos, cambiar los tres colores, cerrar y reabrir la app, y comprobar que todo persiste y que `GET /api/tema.css` lo refleja; luego eliminar la fuente y comprobar que el ajuste vuelve al valor por defecto con aviso
