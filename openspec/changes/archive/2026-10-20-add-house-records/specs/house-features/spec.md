# Spec Delta

## Purpose

Define el catálogo extensible de características opcionales de una casa (servicios, espacios, condición) con sus valores y sus badges personalizables, para que cada casa muestre en los documentos solo lo que tiene.

## ADDED Requirements

### Requirement: Definición de características
El sistema SHALL mantener un catálogo de características. Cada una tiene:
- Etiqueta y categoría (servicios, espacios, condición u otra).
- Tipo de valor: sí/no, número, texto, opción única u opción múltiple.
- Lista de opciones, en los tipos de opción.
- Unidad e icono, ambos opcionales.
- Orden y estado activo/inactivo.

#### Scenario: Crear una característica propia
- **WHEN** el usuario crea la característica "Vista al mar", de tipo sí/no y categoría espacios
- **THEN** la característica aparece en el formulario de todas las casas, dentro de su categoría

#### Scenario: Tipo de opción sin opciones
- **WHEN** el usuario crea una característica de tipo opción única sin definir opciones
- **THEN** el sistema rechaza la característica con un mensaje de validación

### Requirement: Características predefinidas
En una instalación nueva, el sistema SHALL incluir estas características predefinidas:
- Servicios:
  - Gas permanente (sí/no).
  - Respaldo energético (opción múltiple: planta eléctrica, paneles solares, inversor con baterías, otro).
  - Internet (opción única: fibra, ADSL, datos móviles, otro).
  - Seguridad (opción múltiple: rejas, cámaras, alarma, vigilancia, cerca perimetral).
  - Agua (opción múltiple: suministro permanente, cisterna, tanque elevado, pozo).
  - Climatización (opción múltiple: aire acondicionado, ventiladores de techo).
- Espacios:
  - Patio o jardín, terraza, balcón, cuarto de servicio, lavandería y piscina (sí/no).
  - Parqueos (número).
- Condición:
  - Amueblada (opción única: sí, parcialmente, no).
  - Estado de conservación (opción única: nueva, excelente, buena, a remodelar).
  - Año de construcción (número).

Las predefinidas SHALL poder editarse y desactivarse, pero no eliminarse.

#### Scenario: Instalación nueva
- **WHEN** el usuario abre el formulario de una casa en una instalación nueva
- **THEN** ve las características predefinidas agrupadas por categoría

#### Scenario: Intentar eliminar una predefinida
- **WHEN** el usuario intenta eliminar "Gas permanente"
- **THEN** el sistema no lo permite y ofrece desactivarla

#### Scenario: Desactivar una predefinida
- **WHEN** el usuario desactiva "Piscina"
- **THEN** deja de aparecer en el formulario de casas, y las casas que tenían un valor lo conservan pero no lo muestran en sus badges

### Requirement: Valores de características por casa
El sistema SHALL permitir asignar a cada casa un valor por característica, validado según su tipo: los números no negativos y las opciones dentro de la lista definida. Una característica sin valor SHALL significar "no informado" y SHALL no aparecer en los badges de la casa.

#### Scenario: Asignar valores
- **WHEN** el usuario marca "Gas permanente", elige "Fibra" en Internet y "Cámaras" y "Rejas" en Seguridad, y guarda
- **THEN** al reabrir la casa esos valores aparecen tal como se guardaron

#### Scenario: Opción inexistente
- **WHEN** se intenta guardar en Internet un valor que no está entre sus opciones
- **THEN** el sistema rechaza el guardado con un mensaje en esa característica

### Requirement: Badges personalizados por característica
Cada característica SHALL tener una configuración de badge con tres partes:
- Si se muestra como badge.
- Un texto con el marcador opcional `{valor}`.
- Un rol de color: primario, secundario o terciario.

Por defecto, el texto del badge SHALL ser la etiqueta en las características de tipo sí/no, y `etiqueta: {valor}` en las demás.

#### Scenario: Texto de badge personalizado
- **WHEN** el usuario configura el badge de Internet con el texto "Internet {valor}" y el rol secundario
- **THEN** una casa con Internet "Fibra" tiene el badge "Internet Fibra" con rol secundario

#### Scenario: Badge oculto
- **WHEN** el usuario desactiva "mostrar como badge" en "Año de construcción"
- **THEN** ninguna casa muestra ese badge, aunque tenga el valor

### Requirement: Badges resueltos por casa
Al consultar una casa, el sistema SHALL devolver la lista de badges que le corresponden, en el orden del catálogo. La lista incluye solo las características activas, con badge habilitado y con valor. En las de tipo sí/no, el valor SHALL ser "sí". Los valores de opción múltiple SHALL unirse con ", ".

#### Scenario: Badges de una casa
- **WHEN** una casa tiene Gas permanente = sí, Internet = Fibra y Respaldo energético = [Planta eléctrica, Paneles solares]
- **THEN** sus badges son "Gas permanente", "Internet: Fibra" y "Respaldo energético: Planta eléctrica, Paneles solares", cada uno con su rol de color

#### Scenario: Valor "no" en sí/no
- **WHEN** una casa tiene Piscina = no
- **THEN** la lista de badges no incluye "Piscina"

### Requirement: Eliminar características propias
El sistema SHALL permitir eliminar características creadas por el usuario, previa confirmación. Antes de confirmar, SHALL informar cuántas casas tienen un valor para esa característica. Al eliminarla, SHALL borrar esos valores.

#### Scenario: Eliminar característica en uso
- **WHEN** el usuario elimina "Vista al mar", que tienen 3 casas
- **THEN** el sistema avisa que afectará a 3 casas y, al confirmar, elimina la característica y sus valores
