# Design

## Context

Se construye sobre `setup-app-foundation` (servidor con soporte multipart, SQLite y carpeta `data/`) y `add-house-records` (tabla `casa`, su servicio y el formulario). No hay bibliotecas de imagen disponibles en el sistema. Todo debe compilar con MinGW y enlazarse estáticamente, sin DLLs.

## Goals / Non-Goals

**Goals:**
- Que los documentos exportados pesen poco y las fotos salgan orientadas correctamente.
- Que no queden archivos huérfanos ni registros a medias.
- Conservar el original, para poder regenerar las versiones si en la fase de diseño cambian los tamaños.

**Non-Goals:**
- Edición de fotos (recorte, filtros, brillo): el recorte visual en los documentos se resuelve con CSS (`object-fit`).
- Formatos WebP, HEIC y GIF.
- Fotos propias del catálogo, como una portada subida aparte: las cubre `add-document-export`, que reutiliza este procesamiento.

## Decisions

### D1. Bibliotecas stb
- `stb_image` decodifica JPEG y PNG.
- `stb_image_resize2` redimensiona con un filtro de calidad.
- `stb_image_write` codifica JPEG con calidad 85.

Son headers únicos y compilan con MinGW.
- *Alternativas:*
  - libjpeg-turbo y libpng: necesitarían compilación y enlace estático aparte.
  - WIC de Windows: es una API COM verbosa y complica los tests.
- **Riesgo conocido:** `stb_image` no lee EXIF (ver D2) y su JPEG progresivo es más lento, lo cual es aceptable.

### D2. Orientación EXIF propia
Un lector mínimo recorre los segmentos JPEG hasta APP1 `Exif`, interpreta la cabecera TIFF (en ambos órdenes de bytes) y lee el tag `0x0112` de IFD0. Después se aplican al buffer de píxeles las 8 transformaciones posibles (rotaciones y espejos). Son unas 150 líneas, con tests usando JPEG de prueba con orientaciones de 1 a 8.

### D3. Archivos y modelo
```
data/fotos/<casa_id>/<foto_id>_orig.<jpg|png>   original sin tocar
                     <foto_id>_opt.jpg          lado mayor <= 2000 px, q85
                     <foto_id>_min.jpg          lado mayor <= 400 px, q80

foto(id, casa_id -> casa ON DELETE CASCADE, orden, pie,
     formato_original, ancho, alto, bytes_original, creado)
```
- Los nombres usan el `foto_id`, que no se reutiliza, así que nunca colisionan y se pueden servir con caché larga.
- En PNG con canal alfa, la imagen se compone sobre blanco antes de codificarla a JPEG.
- `ancho` y `alto` son los de la versión optimizada y ya orientada. La exportación los usa para saber la proporción.

### D4. Subida atómica, una foto por petición
La interfaz sube los archivos uno a uno a `POST /api/casas/{id}/fotos`. Así puede mostrar el progreso, el error de una foto no afecta a las demás y se evitan peticiones de cientos de MB. El servidor procesa cada foto así:
1. Valida el tamaño (25 MB máx.), la firma del archivo, las dimensiones (`stbi_info`, 60 MP máx.) y el límite de 30 fotos.
2. Decodifica, orienta y genera las versiones en `data/tmp/<uuid>/`.
3. En una transacción, inserta `foto` con `orden = max + 1`, y luego mueve los archivos a `data/fotos/<casa_id>/`.
4. Si algo falla, se revierte la transacción y se borra `data/tmp/<uuid>/`.

Los temporales de más de 1 hora se limpian al arrancar.

### D5. Orden y portada
- `PUT /api/casas/{id}/fotos/orden {ids: [...]}` reescribe `orden` en una sola transacción. Valida que `ids` sea exactamente el conjunto de fotos de la casa.
- La portada es la foto con menor `orden`, sin columna aparte. "Hacer portada" en la interfaz equivale a enviar un nuevo orden con esa foto al principio.

### D6. Eliminación y limpieza
- `DELETE /api/fotos/{id}` borra el registro y después sus tres archivos.
- Al eliminar una casa, el servicio de casas (de `add-house-records`) llama al servicio de fotos para borrar `data/fotos/<casa_id>/` después de eliminar la casa en la base.
- Si al arrancar hay carpetas de fotos de casas que no existen, **solo se registran en el log y no se borran**. Así no se pierden fotos si el usuario restaura un backup de la base.

### D7. API y entrega
- `GET /api/casas/{id}` añade `fotos: [{id, orden, pie, ancho, alto, url_opt, url_min}]`.
- `GET /api/casas` añade `portada_min` (URL o `null`).
- `/media/fotos/<casa_id>/<archivo>` se sirve con `Cache-Control: max-age=31536000, immutable`. Solo se sirven archivos dentro de `data/fotos/`: la ruta se normaliza y se valida para impedir `..`.

### D8. Interfaz
La sección Fotos del formulario de casa tiene:
- Una zona de arrastrar y soltar más un selector de archivos múltiple.
- Una cola de subida con progreso por archivo y errores individuales.
- Una cuadrícula de miniaturas reordenable con SortableJS (vendorizado), donde la primera foto lleva la etiqueta "Portada".
- Acciones por foto: "Hacer portada", editar pie y eliminar con confirmación.

Mientras la casa no tiene `id`, la sección muestra "Guarda la casa para añadir fotos".

## Risks / Trade-offs

- **[Memoria al decodificar fotos grandes]** → El límite de 60 MP (unos 240 MB en RGBA) se comprueba con `stbi_info` antes de decodificar. Como las fotos se procesan una por petición, nunca hay dos decodificaciones a la vez desde la interfaz.
- **[Conservar los originales ocupa disco]** → Es aceptable: permite regenerar las versiones. Se puede añadir más adelante una opción "no conservar originales".
- **[Perfiles de color (ICC) ignorados por stb]** → Las fotos con Display P3 pueden verse algo menos saturadas. Es aceptable para esta fase.
- **[Movimiento de archivos tras el commit]** → Si falla el movimiento después del commit, se elimina el registro y se informa del error. `tmp` y `fotos` están en el mismo volumen, así que el `rename` es atómico.

## Migration Plan

Se añade la siguiente migración disponible, que crea la tabla `foto` con un índice `(casa_id, orden)`.
