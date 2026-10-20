# Design

## Context

Llegan ya construidos:
- Servidor local y SQLite (`setup-app-foundation`).
- Tema CSS con URL base parametrizable y contacto (`add-catalog-settings`).
- Casas y función de resolución de badges (`add-house-records`).
- Fotos optimizadas `_opt.jpg` con `ancho`/`alto` y pipeline de procesamiento reutilizable (`add-house-photos`).

En el spike se verificó que `msedge --headless --print-to-pdf --no-pdf-header-footer file:///...html` respeta `@page { size }`, incrusta las fuentes `@font-face` locales, mantiene el texto como vector y procesa bien UTF-8. No se probó con fotos reales, y eso queda como verificación en las tareas.

Flujo completo:
```
documento + casas + fotos + tema + contacto
        |
        v
  [contexto JSON]  (instantánea)
        |
        +--> plantilla Inja --> HTML vista previa  (/render/..., assets en /media/...)
        |
        '--> plantilla Inja --> HTML exportado (assets/...)  --> <formato>.html   SIEMPRE
                                              |
                                              v
                                 msedge --headless --print-to-pdf  --> <formato>.pdf  INTENTO
```

## Goals / Non-Goals

**Goals:**
- Una sola plantilla por tipo de documento que sirva para la vista previa, el HTML exportado y el PDF.
- Que las plantillas, los CSS y los formatos se editen sin recompilar, porque la fase de diseño trabajará sobre ellos.
- Que un fallo del PDF nunca invalide la exportación.

**Non-Goals:**
- Diseño visual final: las plantillas de este change son neutras y funcionales.
- PNG para Instagram, HTML en un solo archivo, QR, imagen de mapa, marcas de corte o sangrado de imprenta.
- Varios temas por documento.

## Decisions

### D1. Render en servidor con Inja
Las plantillas Jinja-like (`{% if %}`, `{% for %}`, `{% include %}`) se renderizan en C++ a HTML estático.
- *Por qué no renderizar en JS dentro del HTML:* el PDF dependería de que el JS termine antes de imprimir, lo que añade riesgo de tiempos con Edge headless. Además, el HTML estático es más fácil de inspeccionar y de retocar a mano.
- *Alternativa:* Mustache. No tiene lógica, y los bloques opcionales necesitan condicionales.
- Inja es header-only, tiene licencia MIT y usa nlohmann/json, que ya está en el proyecto.

### D2. Carpeta `templates/` junto al exe
```
templates/
  formatos.json        [{id, nombre, ancho, alto, max_badges, ...}]
  base.css             reset, reglas de página, impresión, .no-print, utilidades
  flyer.html           plantilla de flyer
  catalogo.html        plantilla de catálogo
  parciales/           _precio.html, _badges.html, _ubicacion.html, _contacto.html,
                       _foto.html (incluye el bloque neutro), _barra_pdf.html
```
- Cada plantilla recibe `formato`, pone `class="formato-{{ formato.id }}"` en `<body>` y genera `@page { size: {{ancho}}px {{alto}}px; margin: 0 }`. Las diferencias de composición entre formatos se resuelven en CSS con esa clase.
- `cmake --install` copia `templates/`. Un `id` de formato desconocido devuelve 400.

### D3. Contexto del documento
Un constructor de contexto produce un único JSON con:
- `documento`: tipo, título y descripción.
- `casas[]`:
  - Datos fijos y `precio_texto` (ya resuelto: importe formateado con moneda, "Consultar" o `null`).
  - Ubicación, con `mapa_url` si hay coordenadas.
  - `badges` (mediante la función de `add-house-records`).
  - `fotos[]` con `src`, `ancho` y `alto`.
  - `descripcion_corta`.
- `portada`.
- `tema_css`: el CSS del tema con la URL base que corresponda.
- `contacto`, `formato`, `modo` (`preview` | `export`) y `generado_en`.

Toda la lógica de "qué mostrar" (textos por defecto, recorte de descripción y resolución de precio) vive aquí, con tests. Las plantillas solo deciden "dónde y cómo".
- El importe se formatea con separador de miles "," y decimales solo si los hay (ej. `85,000 USD`). Se expone como filtro de plantilla para poder cambiarlo en la fase de diseño.

### D4. Vista previa
`GET /render/{documento_id}/{formato_id}` devuelve el HTML en modo `preview`, con assets en `/media/...` y `Cache-Control: no-store`. La página `#/documentos/{id}` lo muestra en un `<iframe>` escalado con `transform: scale()` para que quepa, y tiene un selector de formato. Al usar la misma plantilla y el mismo contexto, lo que se ve es lo que se exporta.

### D5. Pipeline de exportación
`POST /api/documentos/{id}/exportar {formatos: [...]}` es síncrono. Tarda unos segundos por formato, el servidor es multi-hilo y la interfaz muestra un indicador mientras espera.
1. Adquiere el mutex de exportación. Si está ocupado, responde 409 `exportacion_en_curso`.
2. Construye el contexto una sola vez (es la instantánea) y crea la carpeta de salida con un nombre único (D7).
3. Copia los assets usados:
   - Fotos `_opt.jpg` → `assets/fotos/<foto_id>.jpg`.
   - Portada → `assets/fotos/portada.jpg`.
   - Archivos de las fuentes elegidas → `assets/fuentes/`.
4. Para cada formato, renderiza en modo `export` y escribe `<formato>.html`. El CSS (`base.css` + `tema_css`) va **en línea** dentro de cada HTML, de modo que solo hay que conservar la carpeta `assets/` junto a él. **Desde este punto el HTML está garantizado.**
5. Para cada formato, intenta el PDF (D6) y registra el resultado.
6. Escribe `resultado.json` en la carpeta y devuelve `{carpeta, formatos: [{id, html, pdf, pdf_estado, pdf_motivo}]}`.

Si hay un error antes del paso 4, se borra la carpeta parcial y se devuelve el error. Un error en el paso 5 nunca hace fallar la exportación.

### D6. PDF con Edge headless y perfil propio
- **Comando:** `msedge --headless --disable-gpu --no-first-run --no-pdf-header-footer --user-data-dir=%LOCALAPPDATA%\WCatalog\edge-headless --print-to-pdf=<pdf> file:///<html>`, lanzado con `CREATE_NO_WINDOW`.
- **Perfil separado del de la ventana `--app`:** si se compartiera, Edge podría delegar el comando en la instancia ya abierta en lugar de imprimir.
- **Tiempo máximo:** 30 s + 2 s por página. Si se supera, se ejecuta `TerminateProcess` y el estado es `tiempo_agotado`.
- **Éxito:** el código de salida es 0, el archivo existe, pesa más de 0 bytes y empieza por `%PDF`.
- **Motivos de fallo:** `edge_no_encontrado`, `tiempo_agotado` o `fallo_edge` (con el código de salida).
- La localización de Edge reutiliza el módulo del lanzador de `setup-app-foundation`.

### D7. Nombres de salida
`data/salidas/<AAAA-MM-DD_HHMM>_<slug>_<flyer|catalogo>/`.
- El slug es el título en minúsculas, sin tildes (con la función de plegado de `add-house-records`), con guiones y como máximo 40 caracteres.
- Si la carpeta ya existe, se añade `-2`, `-3`, etc.
- Los archivos internos usan el `id` del formato: `instagram-4x5.html`, `landscape-16x9.pdf`, ...

### D8. Impresión manual desde el HTML
- `base.css` define:
  - `* { print-color-adjust: exact; -webkit-print-color-adjust: exact }`.
  - `@media print { .no-print { display: none } }`.
  - Páginas `<section class="pagina">` con ancho y alto fijos, `overflow: hidden` y `break-after: page`.
- El parcial `_barra_pdf.html` añade una barra fija `.no-print` con el botón "Guardar como PDF" (`window.print()`) y la indicación "Elige *Guardar como PDF* como impresora". Solo se incluye en modo `export`.
- En el PDF de Edge headless la barra no aparece, porque headless también aplica `@media print`.

### D9. Desbordes de contenido
- La descripción se recorta en CSS con `-webkit-line-clamp`, con un número de líneas por formato.
- Los badges se limitan en el contexto a `formato.max_badges` y el resto se cuenta como `badges_restantes`, que la plantilla muestra como "+N".
- El catálogo, en esta fase, usa una página por casa después de la portada. La fase de diseño podrá cambiar a varias casas por página modificando solo las plantillas.

### D10. Modelo de documentos
```
documento(id, tipo CHECK (flyer|catalogo), titulo NULL, descripcion NULL,
          portada_foto_id NULL -> foto ON DELETE SET NULL,
          portada_propia INTEGER, formatos_preferidos JSON, creado, actualizado)
documento_casa(documento_id -> documento CASCADE, casa_id -> casa CASCADE,
               orden, descripcion_corta NULL, PRIMARY KEY(documento_id, casa_id))
```
- En un flyer, `titulo` y `descripcion` en `NULL` significan "usar los de la casa", y tiene exactamente una fila en `documento_casa`.
- La portada subida se procesa con el pipeline de fotos y se guarda en `data/documentos/<id>/portada_{orig,opt,min}.jpg`.
- **Eliminación de casas:** `GET /api/casas/{id}/documentos` alimenta el aviso. Al eliminar la casa, en la misma transacción, se borran los flyers que queden sin casa. Los catálogos pierden la fila por la cascada, y la exportación de un catálogo vacío devuelve 400.

### D11. Apertura de resultados
`POST /api/sistema/abrir-ruta {ruta}` ejecuta `ShellExecuteW` (`open`, o `explore` si es una carpeta) solo si la ruta canonizada está dentro de `data/salidas/`. En cualquier otro caso devuelve 400.

### D12. Interfaz
- `#/documentos`: lista de documentos, y los botones "Nuevo flyer" (elige casa) y "Nuevo catálogo".
- `#/documentos/{id}`:
  - Editor: título, descripción, casas ordenables con descripción corta (en catálogos) y portada.
  - Vista previa con selector de formato.
  - Panel "Exportar" con casillas de formatos (los tres marcados) y el resultado por formato, con los botones Abrir carpeta, Abrir HTML y Abrir PDF.
- En el formulario de casa, el botón "Crear flyer".

## Risks / Trade-offs

- **[Fotos reales hacen el PDF pesado o lento]** → Se usan las versiones `_opt` (2000 px). La verificación integrada mide el tamaño y el tiempo con fotos reales. Si hiciera falta, la fase de diseño puede ajustar el tamaño de `_opt`.
- **[Edge headless bloqueado por políticas o antivirus]** → Es justo el caso del respaldo: el HTML queda listo para guardarse como PDF a mano.
- **[Exportación síncrona sin progreso por formato]** → Es aceptable con 3 formatos. Si los catálogos grandes lo requieren, se pasa a un trabajo en segundo plano con consulta de estado.
- **[Plantillas editables por el usuario]** → Un error de sintaxis Inja se informa con el archivo y la línea, como error 500 con mensaje claro, en lugar de un fallo opaco.
- **[`-webkit-line-clamp` en impresión]** → Chromium lo respeta en el PDF. Se verifica en las tareas.

## Open Questions

- Separadores numéricos definitivos (`85,000` frente a `85.000`) y la posición del símbolo de moneda: se deciden en la fase de diseño, y el filtro de plantilla de D3 lo permite sin cambiar la arquitectura.
