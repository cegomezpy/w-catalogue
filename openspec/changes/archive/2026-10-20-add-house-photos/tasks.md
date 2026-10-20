# Tasks

## 1. Dependencias y esquema

- [x] 1.1 Vendorizar `stb_image.h`, `stb_image_resize2.h` y `stb_image_write.h` en `third_party/`, y SortableJS en `web/vendor/`, registrando versiones y licencias; verificar que el núcleo compila con las implementaciones activadas en una sola unidad de compilación
- [x] 1.2 Añadir la migración de la tabla `foto` con su índice y la clave foránea en cascada; verificar con un test que se aplica sobre la base con casas

## 2. Procesamiento de imágenes

- [x] 2.1 Implementar el lector de orientación EXIF y las 8 transformaciones; verificar con tests usando JPEG de prueba (en `tests/fixtures/`) con orientaciones de 1 a 8
- [x] 2.2 Implementar el pipeline: validar firma, tamaño y megapíxeles, decodificar, orientar, componer el alfa sobre blanco, generar `_opt` (2000 px, q85) y `_min` (400 px, q80) sin ampliar; verificar con tests de dimensiones resultantes (4000×3000 → 2000×1500 / 400×300, 800×600 sin cambios), de PNG con alfa y de un JPEG corrupto rechazado

## 3. Servicio y API de fotos

- [x] 3.1 Implementar la subida atómica (temporal, transacción, movimiento y limpieza en caso de error), el límite de 30 fotos y la limpieza de `data/tmp/` al arrancar; verificar con tests de subida correcta, de fallo sin restos (ni archivos ni registro) y del límite
- [x] 3.2 Implementar el reordenamiento (validando el conjunto de ids), la edición del pie (máx. 120) y la eliminación de foto con borrado de archivos; verificar con tests de reordenamiento, portada tras eliminar la primera y pie demasiado largo
- [x] 3.3 Integrar con el servicio de casas: borrar `data/fotos/<casa_id>/` al eliminar una casa, añadir `fotos` al detalle y `portada_min` a la lista, y registrar en el log las carpetas huérfanas al arrancar; verificar con tests que al eliminar una casa con fotos su carpeta desaparece
- [x] 3.4 Implementar los endpoints de fotos y la entrega de `/media/fotos/...` con caché inmutable y protección de ruta; verificar con tests HTTP de subida multipart, de un formato no admitido (400) y de un intento con `..` (rechazado)

## 4. Interfaz

- [x] 4.1 Crear la sección Fotos del formulario: zona de arrastrar y soltar, selector múltiple, cola de subida con progreso y errores por archivo, y el mensaje para casas sin guardar; verificar manualmente subiendo 5 JPEG y 1 WebP a la vez
- [x] 4.2 Crear la cuadrícula reordenable con SortableJS, la etiqueta "Portada", "Hacer portada", la edición del pie y la eliminación con confirmación; verificar manualmente que el orden persiste al reabrir la casa
- [x] 4.3 Mostrar `portada_min` en la lista de casas y un marcador neutro para las casas sin fotos; verificar manualmente en la lista

## 5. Verificación integrada

- [x] 5.1 De punta a punta con fotos reales de teléfono (verticales con EXIF, y de más de 5 MB): subir, comprobar la orientación, reordenar, cambiar la portada, copiar la carpeta de la app a otra ruta y ver las fotos ahí, y eliminar la casa comprobando que `data/fotos/<id>/` desaparece
