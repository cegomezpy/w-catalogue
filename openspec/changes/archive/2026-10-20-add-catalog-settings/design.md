# Design

## Context

Se construye sobre la arquitectura de `setup-app-foundation`: servidor local, SQLite con migraciones, shell de la interfaz con Alpine.js y carpeta `data/` portable. El render final de los documentos lo hace Chromium (Edge) a partir de HTML/CSS. Por eso la forma natural de transmitir el tema es una hoja CSS con variables y `@font-face`. El spike confirmó que Chromium incrusta en el PDF las fuentes TTF locales declaradas con `@font-face`.

## Goals / Non-Goals

**Goals:**
- Una única fuente de verdad para el tema, que sirva tanto a la vista previa como a la exportación.
- Que las fuentes funcionen sin conexión y viajen dentro de la carpeta de la aplicación.
- Añadir ajustes nuevos en el futuro sin migraciones de esquema.

**Non-Goals:**
- Varios temas con nombre; por ahora hay un solo tema global.
- Logo del negocio, verificador de contraste y paletas sugeridas: se dejan para la fase de diseño.
- Descarga automática de fuentes, y soporte de `.woff`/`.woff2`.

## Decisions

### D1. Ajustes como clave-valor tipado
Se usa la tabla `ajustes(clave TEXT PRIMARY KEY, valor TEXT NOT NULL)`. El servicio de ajustes define el esquema: claves conocidas, tipo, validación y valor por defecto. `GET /api/ajustes` devuelve los valores guardados combinados con los valores por defecto. `PUT` valida todo el objeto y escribe en una sola transacción.
- *Alternativa:* una tabla de una sola fila con una columna por ajuste. Cada ajuste nuevo exigiría una migración.
- **Valores por defecto:**
  - Colores: primario `#1F3A5F`, secundario `#E07A3F`, terciario `#F4EFE6`.
  - Fuentes: títulos `Playfair Display`, texto `Inter`.
  - Moneda: `USD`.
- Los colores se normalizan a mayúsculas.

### D2. Fuentes incluidas: TTF estáticas con manifiesto
`fonts/` vive junto al exe con Inter, Montserrat, Poppins y Playfair Display en pesos 400 y 700, sus licencias OFL y un `fonts/fuentes.json` (familia, archivo, peso, estilo y licencia). Se distribuye con `cmake --install`.
- Se usan instancias estáticas y no fuentes variables, para que la incrustación en el PDF sea predecible y los subconjuntos pequeños.
- Las fuentes incluidas no se guardan en la base de datos: se leen del manifiesto al arrancar.

### D3. Fuentes importadas
- Se guardan en la tabla `fuente(id, familia, peso, estilo, archivo, formato, creado)` y el archivo en `data/fuentes/<slug-familia>-<peso>-<estilo>.<ext>`.
- **Metadatos:** con `stb_truetype` se leen de la tabla `name` el nameID 16 (familia tipográfica, con respaldo al 1) y el 17/2 (subfamilia). Una lectura mínima de la tabla `OS/2` aporta `usWeightClass`, el bit de itálica de `fsSelection` y `fsType`.
- **Validación:** firma (`0x00010000`, `OTTO`, `true`), `stbtt_InitFont` correcto y tamaño de hasta 10 MB.
- **Flujo en dos pasos**, para que el usuario pueda corregir los metadatos:
  1. `POST /api/fuentes/analizar` (multipart) guarda el archivo en `data/tmp/` y devuelve los metadatos detectados y un `token`.
  2. `POST /api/fuentes` con el `token`, los metadatos finales y `reemplazar` opcional mueve el archivo a `data/fuentes/` y crea el registro.
  - Una variante duplicada sin `reemplazar=true` devuelve 409. Los temporales de más de 1 hora se limpian al arrancar.

### D4. Vista unificada de la biblioteca
El servicio de biblioteca combina las fuentes incluidas y las importadas en una lista de familias con sus variantes (`origen: incluida | importada`). Los ajustes guardan el **nombre de familia**. Al resolver el tema, si la familia ya no existe se usa la fuente por defecto correspondiente.

### D5. Generación del tema CSS con URL base parametrizable
Una sola función genera el CSS del tema a partir de los ajustes, la biblioteca y una URL base para los archivos de fuente:
- **Vista previa:** `GET /api/tema.css` con base `/media/fuentes/` y `Cache-Control: no-store`.
- **Exportación** (en `add-document-export`): base `assets/fuentes/`.

Solo se emiten `@font-face` para las familias elegidas, con todas sus variantes. Las pilas de fuentes llevan un respaldo genérico (`serif` o `sans-serif`).

### D6. Abrir enlaces externos de forma segura
`POST /api/sistema/abrir-enlace {url}` ejecuta `ShellExecuteW` solo si la URL es `https://`. Cualquier otro esquema devuelve 400. Así se evita usar el endpoint para abrir archivos o ejecutables locales.

### D7. Eliminar una fuente en uso
Al eliminar una variante, si su familia queda sin variantes y estaba elegida en los ajustes, el servicio restablece ese ajuste a la fuente por defecto en la misma transacción. La respuesta incluye un arreglo `avisos` que la interfaz muestra al usuario.

### D8. Interfaz de Ajustes
La página `#/ajustes` tiene secciones Tema, Tipografía, Contacto y General.
- Los colores usan `<input type="color">` sincronizado con un campo hex.
- Una tarjeta de vista previa carga `tema.css` (con un parámetro anti-caché) y muestra un título, un párrafo y tres badges con los roles de color.
- La biblioteca de fuentes registra cada familia con la API `FontFace` del navegador, para mostrarla al instante sin recargar.

## Risks / Trade-offs

- **[Fuentes importadas con licencia que restringe la incrustación (`fsType` restringido)]** → Se muestra una advertencia al importar. La responsabilidad de la licencia es del usuario.
- **[Familias con nombres inconsistentes entre archivos]** → El usuario puede corregir familia, peso y estilo en el paso 2 de la importación.
- **[OTF con contornos CFF]** → `stb_truetype` lee las tablas `name` y `OS/2` igual que en TTF. Chromium renderiza CFF sin problema.
- **[Clave-valor sin tipos en la base]** → La validación vive centralizada en el servicio, con tests por clave.

## Migration Plan

Se añade la siguiente migración disponible, que crea `ajustes` y `fuente`. No hace falta poblar `ajustes`, porque los valores por defecto viven en el servicio.
