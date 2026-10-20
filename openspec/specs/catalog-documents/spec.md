# catalog-documents Specification

## Purpose
Permite componer y guardar los documentos que se exportan, sean flyers de una casa o catálogos de varias casas, elegir sus formatos de salida y verlos en vista previa tal como se exportarán.

## Requirements

### Requirement: Formatos de salida
El sistema SHALL ofrecer estos formatos de salida:
- "Instagram 4:5": 1080×1350 px.
- "Landscape 16:9": 1920×1080 px.
- "Móvil 9:16": 1080×1920 px.

Los formatos SHALL estar definidos como datos, de modo que añadir uno no requiera recompilar la aplicación.

#### Scenario: Formatos disponibles
- **WHEN** el usuario va a exportar un documento
- **THEN** puede elegir entre Instagram 4:5, Landscape 16:9 y Móvil 9:16, con los tres marcados por defecto

### Requirement: Crear flyer
El sistema SHALL permitir crear un flyer asociado a exactamente una casa. Por defecto, el título del flyer SHALL ser el título de la casa y su descripción la de la casa, y ambos SHALL poder cambiarse solo para el flyer.

#### Scenario: Flyer desde una casa
- **WHEN** el usuario pulsa "Crear flyer" en la casa "Casa en Miramar"
- **THEN** se crea un flyer con el título "Casa en Miramar" y se abre su vista previa

#### Scenario: Título propio del flyer
- **WHEN** el usuario cambia el título del flyer a "¡Oportunidad en Miramar!"
- **THEN** el flyer usa ese título y la casa conserva el suyo

### Requirement: Crear catálogo
El sistema SHALL permitir crear un catálogo con un título obligatorio, una descripción opcional y una lista ordenada de una o más casas. Cada casa del catálogo SHALL tener una descripción corta. Si el usuario no la escribe, se usa la descripción de la casa recortada a 180 caracteres sin cortar palabras y terminada en "…".

#### Scenario: Catálogo con tres casas
- **WHEN** el usuario crea el catálogo "Casas en La Habana" con tres casas en un orden concreto
- **THEN** el catálogo guarda esas casas en ese orden

#### Scenario: Catálogo sin casas
- **WHEN** el usuario intenta guardar un catálogo sin ninguna casa
- **THEN** el sistema rechaza el guardado indicando que hace falta al menos una casa

#### Scenario: Descripción corta automática
- **WHEN** una casa del catálogo tiene una descripción de 400 caracteres y no tiene descripción corta propia
- **THEN** el catálogo muestra los primeros 180 caracteres aproximadamente, terminando en una palabra completa y "…"

### Requirement: Portada del catálogo
El sistema SHALL permitir dar a un catálogo una imagen de portada opcional. Puede subirse (JPEG o PNG, con la misma validación y optimización que las fotos de casas) o elegirse entre las fotos de las casas incluidas.

#### Scenario: Portada subida
- **WHEN** el usuario sube una imagen como portada del catálogo
- **THEN** la portada se guarda corregida y optimizada y aparece en la vista previa

#### Scenario: Portada desde una casa
- **WHEN** el usuario elige como portada una foto de una de las casas incluidas
- **THEN** el catálogo usa esa foto en su portada

### Requirement: Gestión de documentos
El sistema SHALL listar los documentos guardados (tipo, título y fecha de modificación) y SHALL permitir abrirlos, editarlos y eliminarlos previa confirmación. Los documentos SHALL mostrar siempre los datos vigentes de sus casas.

#### Scenario: Cambio en una casa
- **WHEN** el usuario cambia el precio de una casa incluida en un catálogo
- **THEN** la vista previa del catálogo muestra el precio nuevo

### Requirement: Eliminación de casas usadas en documentos
Al eliminar una casa, el sistema SHALL avisar qué documentos la usan. Al confirmar, SHALL quitarla de los catálogos y eliminar los flyers de esa casa. Un catálogo que quede sin casas SHALL conservarse, pero no podrá exportarse hasta que tenga al menos una.

#### Scenario: Casa usada en un flyer y un catálogo
- **WHEN** el usuario elimina una casa que tiene un flyer y forma parte de un catálogo
- **THEN** el sistema lista ambos documentos en la confirmación y, al confirmar, elimina el flyer y quita la casa del catálogo

### Requirement: Vista previa por formato
El sistema SHALL mostrar la vista previa de un documento en cada formato con el mismo resultado visual que la exportación. SHALL usar los datos, el tema y la tipografía vigentes.

#### Scenario: Cambiar de formato en la vista previa
- **WHEN** el usuario cambia la vista previa de Instagram 4:5 a Móvil 9:16
- **THEN** la vista previa muestra el documento con las proporciones y la composición del formato móvil

#### Scenario: Cambio de tema
- **WHEN** el usuario cambia el color primario en Ajustes y vuelve a la vista previa
- **THEN** la vista previa refleja el nuevo color
