# Proposal

## Why

Un catálogo inmobiliario vende con fotos. El usuario necesita añadir las fotos de cada casa, ordenarlas y elegir la portada. Las fotos de un teléfono pesan varios MB y a menudo vienen rotadas según los datos EXIF. Si se usan tal cual, los HTML y PDF exportados pesarán decenas de MB y algunas fotos saldrán giradas. Por eso hace falta una gestión de fotos con optimización antes de exportar documentos.

## What Changes

- Subida de una o varias fotos JPEG/PNG por casa, con validación de formato, tamaño y resolución.
- Procesamiento al subir:
  - Corrección de la orientación EXIF.
  - Una versión optimizada para documentos (lado mayor de hasta 2000 px).
  - Una miniatura para la interfaz (lado mayor de hasta 400 px).
  - Se conserva el original.
- Orden de las fotos y elección de portada. La portada es la primera foto.
- Pie de foto opcional.
- Eliminación de fotos y limpieza de los archivos al eliminar una casa.
- La lista de casas muestra la miniatura de la portada.
- Sección **Fotos** en el formulario de la casa: zona de arrastrar y soltar, cuadrícula reordenable, portada, pie y eliminar.

## Capabilities

### New Capabilities
- `house-photos`: subida, validación, optimización, orden, portada, pie, eliminación y entrega de las fotos de cada casa.

### Modified Capabilities
<!-- Ninguna: `house-records` aún no está archivada. Este change amplía su comportamiento (miniatura en la lista y limpieza de fotos al eliminar una casa) mediante requisitos propios de `house-photos`. -->

## Impact

- **Depende de:** `setup-app-foundation` y `add-house-records`.
- **Base de datos:** nueva migración con la tabla `foto`.
- **API:**
  - `POST /api/casas/{id}/fotos`
  - `PUT /api/casas/{id}/fotos/orden`
  - `PUT/DELETE /api/fotos/{id}`
  - `GET /media/fotos/...`
  - Además, se amplían las respuestas de `GET /api/casas` (portada) y `GET /api/casas/{id}` (fotos).
- **Archivos:** `data/fotos/<casa_id>/`.
- **Dependencias nuevas:** `stb_image`, `stb_image_resize2` y `stb_image_write` (dominio público/MIT) en el núcleo, y SortableJS (MIT) en la interfaz.
- **Lo consumirá:** `add-document-export`, que usa las versiones optimizadas.
