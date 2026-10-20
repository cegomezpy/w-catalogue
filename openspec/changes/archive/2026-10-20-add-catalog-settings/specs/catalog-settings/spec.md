# Spec Delta

## Purpose

Permite al usuario definir la identidad visual (tres colores y fuentes) y los datos de contacto que llevarán todos sus flyers y catálogos, y la publica como un tema CSS reutilizable.

## ADDED Requirements

### Requirement: Colores del tema
El sistema SHALL guardar tres colores del tema: primario, secundario y terciario. Cada uno SHALL ser un color hexadecimal `#RRGGBB`. En una instalación nueva, los colores SHALL tener valores por defecto.

#### Scenario: Cambiar un color
- **WHEN** el usuario elige un nuevo color primario y guarda
- **THEN** el color queda guardado y la vista previa de Ajustes lo muestra

#### Scenario: Color inválido
- **WHEN** el usuario guarda un color con formato distinto de `#RRGGBB` (por ejemplo `azul` o `#12`)
- **THEN** el sistema rechaza el cambio con un mensaje que indica el formato esperado y conserva el valor anterior

#### Scenario: Valores por defecto
- **WHEN** la aplicación arranca por primera vez
- **THEN** los tres colores tienen valores por defecto válidos

### Requirement: Fuentes de títulos y de texto
El sistema SHALL permitir elegir una familia de fuente para títulos y otra para texto entre las fuentes disponibles en la biblioteca. Si la fuente elegida deja de existir, SHALL usar la fuente por defecto correspondiente.

#### Scenario: Elegir fuentes
- **WHEN** el usuario elige "Playfair Display" para títulos e "Inter" para texto y guarda
- **THEN** las elecciones quedan guardadas y la vista previa muestra los títulos y el texto con esas fuentes

#### Scenario: Fuente elegida eliminada
- **WHEN** la fuente de títulos elegida es una fuente importada y el usuario la elimina
- **THEN** la fuente de títulos vuelve a la fuente por defecto y el usuario recibe un aviso

### Requirement: Datos de contacto
El sistema SHALL guardar datos de contacto opcionales: nombre del negocio o agente, teléfono, WhatsApp, email, sitio web, Instagram y Facebook. Un email informado SHALL tener formato válido.

#### Scenario: Contacto parcial
- **WHEN** el usuario guarda solo el nombre y el WhatsApp
- **THEN** esos datos quedan guardados y el resto queda vacío sin error

#### Scenario: Email inválido
- **WHEN** el usuario guarda un email sin `@`
- **THEN** el sistema rechaza el cambio con un mensaje de validación

### Requirement: Moneda por defecto
El sistema SHALL guardar una moneda por defecto (código de texto corto, por ejemplo `USD`) que se propone al registrar precios. En una instalación nueva, su valor SHALL ser `USD`.

#### Scenario: Cambiar moneda por defecto
- **WHEN** el usuario cambia la moneda por defecto a `EUR`
- **THEN** los nuevos registros de precio proponen `EUR`

### Requirement: Tema publicado como CSS
El sistema SHALL publicar la configuración vigente como una hoja de estilos que define las variables `--color-primario`, `--color-secundario`, `--color-terciario`, `--fuente-titulos` y `--fuente-texto`. La hoja SHALL incluir las reglas `@font-face` de las fuentes elegidas.

#### Scenario: Tema actualizado
- **WHEN** el usuario cambia un color o una fuente y guarda
- **THEN** la siguiente solicitud de la hoja de estilos del tema refleja el nuevo valor

#### Scenario: Fuentes incluidas en el tema
- **WHEN** la fuente de títulos es una fuente importada
- **THEN** la hoja de estilos del tema contiene su `@font-face` con todos los pesos y estilos importados de esa familia
