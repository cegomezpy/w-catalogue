# font-library Specification

## Purpose

Gestiona las fuentes tipográficas disponibles para los documentos: las incluidas con la aplicación y las que el usuario importa desde archivos, para que funcionen sin conexión y viajen con los datos.

## Requirements

### Requirement: Fuentes incluidas
El sistema SHALL ofrecer un conjunto de fuentes de licencia libre incluidas con la aplicación, con al menos los pesos normal y negrita. Estas fuentes SHALL estar disponibles sin conexión y SHALL no poder eliminarse.

#### Scenario: Lista de fuentes en instalación nueva
- **WHEN** el usuario abre la biblioteca de fuentes en una instalación nueva
- **THEN** ve las fuentes incluidas, marcadas como "Incluida" y sin opción de eliminar

### Requirement: Importar fuente desde archivo
El sistema SHALL permitir importar archivos de fuente `.ttf` u `.otf` de hasta 10 MB. SHALL leer del propio archivo el nombre de la familia, el peso y el estilo, y el usuario SHALL poder corregirlos. Los archivos importados SHALL guardarse dentro de la carpeta de datos, de modo que viajen con ella.

#### Scenario: Importación correcta
- **WHEN** el usuario importa `Lato-Bold.ttf`
- **THEN** la fuente aparece en la biblioteca como familia "Lato", peso 700, estilo normal, y queda disponible para elegirla en Ajustes

#### Scenario: Varias variantes de una familia
- **WHEN** el usuario importa `Lato-Regular.ttf` y luego `Lato-Bold.ttf`
- **THEN** la biblioteca muestra una sola familia "Lato" con dos variantes

#### Scenario: Archivo no válido
- **WHEN** el usuario importa un archivo que no es una fuente TrueType/OpenType válida, o que supera 10 MB
- **THEN** el sistema rechaza la importación con un mensaje claro y no guarda nada

#### Scenario: Variante duplicada
- **WHEN** el usuario importa una variante con la misma familia, peso y estilo que una ya importada
- **THEN** el sistema le pide confirmar si reemplaza la existente, y solo la reemplaza si confirma

### Requirement: Vista previa de fuentes
El sistema SHALL mostrar cada familia de la biblioteca con un texto de muestra en español que incluya tildes y ñ.

#### Scenario: Vista previa de una fuente importada
- **WHEN** el usuario importa una fuente
- **THEN** la ve renderizada con el texto de muestra sin reiniciar la aplicación

### Requirement: Eliminar fuente importada
El sistema SHALL permitir eliminar una fuente importada, previa confirmación. Al eliminarla, SHALL borrar su archivo de la carpeta de datos.

#### Scenario: Eliminación
- **WHEN** el usuario elimina una variante importada y confirma
- **THEN** la variante desaparece de la biblioteca y su archivo se borra de la carpeta de datos

### Requirement: Acceso a Google Fonts
El sistema SHALL ofrecer una acción que abre Google Fonts en el navegador predeterminado del usuario para descargar fuentes que después se importan.

#### Scenario: Buscar fuentes en línea
- **WHEN** el usuario pulsa "Buscar fuentes en Google Fonts"
- **THEN** se abre `https://fonts.google.com` en su navegador predeterminado
