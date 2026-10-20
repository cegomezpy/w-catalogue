# Spec Delta

## Purpose

Convierte flyers y catálogos en archivos entregables. Garantiza siempre un HTML+CSS autocontenido e intenta además un PDF por formato, de modo que un fallo del PDF nunca deje al usuario sin resultado.

## ADDED Requirements

### Requirement: HTML+CSS siempre disponible
Al exportar un documento, el sistema SHALL generar siempre, por cada formato elegido, un archivo HTML con su CSS. Ese HTML SHALL mostrar el documento correctamente al abrirse desde el disco, sin la aplicación en ejecución y sin conexión a internet. Las fotos y fuentes que use SHALL copiarse a la carpeta de salida.

#### Scenario: Abrir el HTML sin la app
- **WHEN** el usuario exporta un flyer, cierra la aplicación y abre el HTML exportado con doble clic sin conexión a internet
- **THEN** el documento se ve con sus fotos, colores y fuentes

#### Scenario: Carpeta de salida movida
- **WHEN** el usuario copia la carpeta de una exportación a otra máquina
- **THEN** el HTML se sigue viendo correctamente

### Requirement: PDF como intento con respaldo
Después de generar los HTML, el sistema SHALL intentar crear un PDF por cada formato elegido, con el tamaño de página exacto del formato, el texto seleccionable y las fuentes incrustadas. Si el PDF de un formato no puede generarse, el sistema SHALL conservar los HTML, continuar con los demás formatos e informar el motivo por formato.

#### Scenario: Exportación completa
- **WHEN** el usuario exporta un flyer en los tres formatos y Edge funciona
- **THEN** la carpeta de salida contiene tres HTML y tres PDF, y cada PDF tiene el tamaño de página de su formato

#### Scenario: Edge no disponible
- **WHEN** el usuario exporta y Edge no se encuentra o falla
- **THEN** la carpeta de salida contiene los HTML, el resultado indica "PDF no generado" con el motivo, e invita a abrir el HTML y usar "Guardar como PDF"

#### Scenario: Tiempo agotado
- **WHEN** la generación del PDF de un formato supera su tiempo máximo
- **THEN** el sistema detiene ese intento, lo informa como fallido por tiempo y conserva el HTML

### Requirement: HTML preparado para guardar como PDF manualmente
El HTML exportado SHALL incluir un botón "Guardar como PDF" que abre el diálogo de impresión del navegador, junto con una indicación breve de cómo usarlo. El botón y la indicación SHALL no aparecer en lo impreso. Al imprimir, el HTML SHALL producir páginas con el tamaño del formato, sin márgenes y con los colores y fondos tal como se ven en pantalla.

#### Scenario: Guardar como PDF desde el navegador
- **WHEN** el usuario abre el HTML en Edge o Chrome, pulsa "Guardar como PDF" y elige "Guardar como PDF" en el diálogo
- **THEN** obtiene un PDF con el tamaño de página del formato, con fondos y colores, y sin el botón

### Requirement: Contenido opcional sin huecos
Los documentos SHALL omitir todo bloque cuyo dato no exista, sin dejar espacios vacíos:
- **Precio:** si la casa no tiene precio, se muestra "Consultar" o nada, según la opción de la casa.
- **Coordenadas:** si no las hay, no se muestran las coordenadas ni el enlace al mapa.
- **Fotos:** si la casa no tiene fotos, se muestra un bloque neutro con los colores del tema.
- **Badges y contacto:** si no los hay, no aparecen sus secciones.

#### Scenario: Casa mínima
- **WHEN** se exporta el flyer de una casa que solo tiene título
- **THEN** el documento muestra el título, el bloque neutro de foto y el precio "Consultar", sin secciones vacías

#### Scenario: Precio oculto
- **WHEN** la casa no tiene precio y su opción es "ocultar"
- **THEN** el documento no muestra ningún texto de precio

#### Scenario: Con coordenadas
- **WHEN** la casa tiene coordenadas
- **THEN** el documento muestra la ubicación con un enlace que abre esas coordenadas en Google Maps

### Requirement: Estructura de páginas
Un flyer SHALL ocupar exactamente una página en cada formato. Un catálogo SHALL tener una página de portada (título, descripción, portada y contacto) seguida del contenido de sus casas. Si el contenido de una página no cabe, los textos largos SHALL recortarse con "…" y los badges sobrantes SHALL resumirse como "+N", sin que el contenido se desborde a otra página.

#### Scenario: Flyer con muchos badges
- **WHEN** una casa tiene 14 badges y el formato admite 8
- **THEN** el flyer muestra 8 badges y un indicador "+6", en una sola página

### Requirement: Carpeta y nombres de salida
Cada exportación SHALL crear una carpeta nueva dentro de la carpeta de datos, con el nombre `<fecha-hora>_<titulo-sin-tildes>_<flyer|catalogo>`. Dentro, SHALL contener un archivo por formato (`<formato>.html` y, si se generó, `<formato>.pdf`) y una carpeta `assets/`. Una exportación SHALL no sobrescribir nunca otra anterior.

#### Scenario: Dos exportaciones seguidas
- **WHEN** el usuario exporta el mismo documento dos veces en el mismo minuto
- **THEN** existen dos carpetas distintas y la primera no se modifica

### Requirement: Exportación como instantánea
Los archivos exportados SHALL reflejar los datos del momento de la exportación y SHALL no cambiar si después se modifican las casas, las fotos o el tema.

#### Scenario: Editar después de exportar
- **WHEN** el usuario exporta un flyer y luego cambia el precio de la casa
- **THEN** el HTML y el PDF ya exportados conservan el precio anterior

### Requirement: Resultado y apertura de archivos
Al terminar una exportación, el sistema SHALL mostrar el resultado de cada formato (HTML generado, y PDF generado o fallido con su motivo). SHALL permitir abrir la carpeta de salida, cada HTML y cada PDF con la aplicación predeterminada de Windows.

#### Scenario: Abrir la carpeta
- **WHEN** el usuario pulsa "Abrir carpeta" en el resultado
- **THEN** se abre el Explorador de Windows en la carpeta de esa exportación

### Requirement: Una exportación a la vez
El sistema SHALL procesar una sola exportación a la vez. Si se solicita otra mientras una está en curso, SHALL rechazarla con un mensaje que indica que hay una exportación en curso.

#### Scenario: Doble clic en exportar
- **WHEN** el usuario pulsa "Exportar" dos veces seguidas
- **THEN** solo se realiza una exportación y la segunda petición recibe el aviso
