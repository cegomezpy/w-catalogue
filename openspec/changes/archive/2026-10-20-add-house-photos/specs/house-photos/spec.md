# Spec Delta

## Purpose

Gestiona las fotos de cada casa: subida, corrección de orientación, versiones optimizadas para documentos y miniaturas, orden, portada y eliminación, con los archivos guardados dentro de la carpeta de datos portable.

## ADDED Requirements

### Requirement: Subir fotos a una casa
El sistema SHALL permitir subir una o varias fotos a una casa ya guardada. Se aceptan JPEG y PNG de hasta 25 MB y hasta 60 megapíxeles cada una. Una casa SHALL tener como máximo 30 fotos. Las fotos nuevas SHALL añadirse al final del orden.

#### Scenario: Subida múltiple
- **WHEN** el usuario selecciona 5 fotos JPEG para una casa sin fotos
- **THEN** las 5 fotos aparecen en la casa en el orden de selección y la primera queda como portada

#### Scenario: Formato no admitido
- **WHEN** el usuario sube un archivo WebP, HEIC o un archivo que no es una imagen
- **THEN** el sistema lo rechaza con un mensaje que indica los formatos admitidos, sin afectar a las demás fotos subidas a la vez

#### Scenario: Límite de fotos
- **WHEN** una casa tiene 30 fotos y el usuario sube otra
- **THEN** el sistema rechaza la subida indicando el límite

#### Scenario: Casa sin guardar
- **WHEN** el usuario está creando una casa que aún no ha guardado
- **THEN** la sección de fotos indica que primero hay que guardar la casa y no permite subir

### Requirement: Orientación y versiones optimizadas
Al subir una foto, el sistema SHALL corregir su orientación según los datos EXIF. Además, SHALL generar dos versiones:
- Una versión optimizada para documentos, con el lado mayor de hasta 2000 px.
- Una miniatura, con el lado mayor de hasta 400 px.

El sistema SHALL conservar el archivo original. Las imágenes más pequeñas que esos límites SHALL no ampliarse.

#### Scenario: Foto vertical de teléfono
- **WHEN** el usuario sube una foto vertical de teléfono guardada horizontalmente con orientación EXIF 6
- **THEN** la miniatura y la versión optimizada se ven verticales y correctamente orientadas

#### Scenario: Foto grande
- **WHEN** el usuario sube una foto de 4000×3000 px
- **THEN** la versión optimizada mide 2000×1500 px y la miniatura 400×300 px

#### Scenario: PNG con transparencia
- **WHEN** el usuario sube un PNG con transparencia
- **THEN** las versiones generadas muestran las zonas transparentes sobre fondo blanco

### Requirement: Subida sin estados intermedios
Si el procesamiento de una foto falla, el sistema SHALL no dejar ni registro ni archivos de esa foto.

#### Scenario: Imagen corrupta
- **WHEN** el usuario sube un JPEG truncado o corrupto
- **THEN** el sistema informa el error y no queda ningún archivo ni registro de esa foto

### Requirement: Orden y portada
El sistema SHALL permitir reordenar las fotos de una casa. La portada SHALL ser siempre la primera foto del orden, y elegir una foto como portada SHALL moverla a la primera posición.

#### Scenario: Elegir portada
- **WHEN** el usuario marca como portada la tercera foto
- **THEN** esa foto pasa a la primera posición y las demás conservan su orden relativo

#### Scenario: Reordenar
- **WHEN** el usuario arrastra la última foto a la segunda posición
- **THEN** el nuevo orden se guarda y se mantiene al reabrir la casa

### Requirement: Pie de foto
El sistema SHALL permitir un pie de foto opcional de hasta 120 caracteres por foto.

#### Scenario: Pie demasiado largo
- **WHEN** el usuario escribe un pie de 150 caracteres
- **THEN** el sistema rechaza el guardado indicando el máximo

### Requirement: Eliminar fotos
El sistema SHALL permitir eliminar una foto previa confirmación, borrando su registro y todos sus archivos. Al eliminar una casa, SHALL eliminar también todas sus fotos y archivos.

#### Scenario: Eliminar la portada
- **WHEN** el usuario elimina la foto que era la portada
- **THEN** la siguiente foto del orden pasa a ser la portada

#### Scenario: Eliminar una casa con fotos
- **WHEN** el usuario elimina una casa con 8 fotos
- **THEN** no queda ningún archivo de esas fotos en la carpeta de datos

### Requirement: Miniaturas en la interfaz
La lista de casas SHALL mostrar la miniatura de la portada de cada casa. Las casas sin fotos SHALL mostrar un marcador neutro.

#### Scenario: Lista con portadas
- **WHEN** el usuario abre la lista de casas
- **THEN** cada casa con fotos muestra la miniatura de su portada y las demás un marcador

### Requirement: Fotos dentro de la carpeta de datos
El sistema SHALL guardar todos los archivos de fotos dentro de la carpeta de datos portable, de modo que viajen con ella y se muestren sin conexión.

#### Scenario: Copia a otra máquina
- **WHEN** el usuario copia la carpeta de la aplicación a otra máquina sin internet
- **THEN** todas las fotos de todas las casas se muestran correctamente
