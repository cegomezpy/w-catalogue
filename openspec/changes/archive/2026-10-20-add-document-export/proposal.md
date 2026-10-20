# Proposal

## Why

La salida del producto es el documento: un flyer de una casa o un catálogo de varias casas, en los formatos de redes sociales (Instagram), móvil y PC (landscape). Con casas, fotos y ajustes ya disponibles, falta componerlos y exportarlos. El PDF depende de Edge, y si su generación falla el usuario no puede quedarse sin resultado. Por eso la exportación **siempre** produce HTML+CSS autocontenido, desde el que también se puede guardar el PDF manualmente.

## What Changes

- **Documentos guardados de dos tipos:**
  - **Flyer:** una sola casa.
  - **Catálogo:** varias casas ordenadas, con título, descripción, portada opcional (subida o elegida entre las fotos de las casas) y descripción corta por casa.
- **Formatos definidos como datos:**
  - Instagram 4:5 (1080×1350).
  - Landscape 16:9 (1920×1080).
  - Móvil 9:16 (1080×1920).
- **Vista previa** en la app de cada documento en cada formato, renderizada con la misma plantilla que la exportación.
- **Exportación en varios formatos a la vez** (por defecto los tres):
  1. Siempre genera, por formato, un HTML+CSS en una carpeta de salida con sus recursos (fotos optimizadas y fuentes). Ese HTML se abre sin la app y sin internet.
  2. Después intenta generar el PDF de cada formato con Edge en modo headless. Si falla, el HTML se conserva y se informa el motivo.
  3. El HTML incluye un botón "Guardar como PDF", oculto al imprimir, y está preparado para imprimirse con el tamaño exacto.
- **Plantillas básicas funcionales** (neutras, con el tema del usuario) en las que los bloques opcionales desaparecen sin dejar huecos. El diseño visual definitivo es una fase posterior y se hará sobre estas mismas plantillas.
- **Acciones sobre el resultado:** abrir la carpeta, el HTML o el PDF.

## Capabilities

### New Capabilities
- `catalog-documents`: creación y gestión de flyers y catálogos, formatos disponibles, selección y orden de casas, portada, y vista previa por formato.
- `document-export`: exportación a HTML+CSS garantizada, intento de PDF con respaldo, estructura de la carpeta de salida, preparación del HTML para impresión manual, contenido opcional sin huecos y apertura de los resultados.

### Modified Capabilities
<!-- Ninguna: las capabilities de las que depende aún no están archivadas. La interacción con la eliminación de casas se define en los requisitos de `catalog-documents`. -->

## Impact

- **Depende de:** `setup-app-foundation`, `add-catalog-settings` (tema CSS y contacto), `add-house-records` (datos y badges) y `add-house-photos` (fotos optimizadas y su pipeline, que se reutiliza para la portada).
- **Base de datos:** nueva migración con `documento` y `documento_casa`.
- **API:**
  - `/api/documentos` (CRUD)
  - `POST /api/documentos/{id}/portada`
  - `POST /api/documentos/{id}/exportar`
  - `GET /api/formatos`
  - `GET /render/{id}/{formato}` (vista previa)
  - `POST /api/sistema/abrir-ruta`
- **Archivos:**
  - `templates/` junto al exe: plantillas, CSS base y `formatos.json`.
  - `data/documentos/` (portadas subidas).
  - `data/salidas/` (exportaciones).
- **Dependencia nueva:** Inja (MIT, header-only, usa nlohmann/json).
- **Sistema:** Edge headless, con un perfil propio separado del de la ventana de la app.
- **Fuera de alcance**, para fases siguientes:
  - Exportar PNG para Instagram.
  - HTML en un solo archivo.
  - Código QR y mapa.
  - Diseño visual definitivo de las plantillas.
