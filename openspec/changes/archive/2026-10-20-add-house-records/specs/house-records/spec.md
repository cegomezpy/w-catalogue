# Spec Delta

## Purpose

Permite registrar y mantener las casas que aparecerán en flyers y catálogos, con sus datos fijos, precio opcional, estado de disponibilidad y ubicación con coordenadas opcionales.

## ADDED Requirements

### Requirement: Crear casa con datos mínimos
El sistema SHALL permitir crear una casa con solo el título como dato obligatorio. Todos los demás datos SHALL ser opcionales. Una casa nueva SHALL tener el estado "disponible" por defecto.

#### Scenario: Casa solo con título
- **WHEN** el usuario crea una casa con el título "Casa en Miramar" y nada más
- **THEN** la casa queda guardada con estado "disponible" y aparece en la lista de casas

#### Scenario: Título vacío
- **WHEN** el usuario intenta guardar una casa con el título vacío o compuesto solo de espacios
- **THEN** el sistema rechaza el guardado con un mensaje que indica que el título es obligatorio

### Requirement: Datos fijos de la casa
El sistema SHALL guardar los siguientes datos opcionales de cada casa:
- Descripción.
- Operación: venta o alquiler.
- Cuartos, baños, comedores independientes y niveles: enteros no negativos.
- Área construida y área de terreno: en m², números no negativos.

#### Scenario: Datos completos
- **WHEN** el usuario guarda una casa con 4 cuartos, 2 baños, 2 comedores independientes y 120 m² construidos
- **THEN** al abrir la casa se muestran exactamente esos valores

#### Scenario: Valor negativo
- **WHEN** el usuario introduce -1 en cuartos
- **THEN** el sistema rechaza el guardado con un mensaje en ese campo

### Requirement: Precio opcional
El sistema SHALL permitir que una casa tenga precio o no. Si tiene precio, SHALL guardar el importe (no negativo, con hasta 2 decimales) y la moneda, que por defecto es la moneda configurada en Ajustes. Para el caso sin precio, cada casa SHALL indicar si en los documentos se muestra "Consultar" o se oculta el precio. El valor por defecto SHALL ser "Consultar".

#### Scenario: Casa sin precio
- **WHEN** el usuario guarda una casa sin importe de precio
- **THEN** la casa se guarda sin error, sin precio y con la opción "Consultar"

#### Scenario: Moneda propuesta
- **WHEN** la moneda por defecto en Ajustes es `EUR` y el usuario introduce un precio en una casa nueva
- **THEN** el formulario propone `EUR` como moneda

### Requirement: Estado de la casa
El sistema SHALL guardar el estado de cada casa, que es uno de: disponible, reservada, vendida o alquilada.

#### Scenario: Marcar como vendida
- **WHEN** el usuario cambia el estado de una casa a "vendida"
- **THEN** la lista muestra la casa con el estado "vendida"

### Requirement: Ubicación con coordenadas opcionales
El sistema SHALL guardar una ubicación opcional compuesta por dirección, barrio o sector, municipio, provincia y coordenadas. Si hay coordenadas, SHALL incluir tanto la latitud (entre -90 y 90) como la longitud (entre -180 y 180). El sistema SHALL aceptar las coordenadas pegadas en el formato `lat, lon` que copia Google Maps.

#### Scenario: Pegar coordenadas
- **WHEN** el usuario pega `23.1136, -82.3666` en el campo de coordenadas
- **THEN** la casa guarda la latitud 23.1136 y la longitud -82.3666

#### Scenario: Coordenadas incompletas o fuera de rango
- **WHEN** el usuario introduce solo la latitud, o una latitud de 95
- **THEN** el sistema rechaza el guardado con un mensaje sobre las coordenadas

#### Scenario: Sin coordenadas
- **WHEN** el usuario guarda una casa sin coordenadas
- **THEN** la casa se guarda sin error y sin coordenadas

### Requirement: Listado y búsqueda de casas
El sistema SHALL listar las casas ordenadas por fecha de última modificación, de la más reciente a la más antigua. SHALL permitir buscar por texto en el título, el barrio o sector y el municipio, y filtrar por estado.

#### Scenario: Búsqueda por sector
- **WHEN** el usuario busca "vedado"
- **THEN** la lista muestra solo las casas cuyo título, barrio o sector, o municipio contienen "Vedado", sin distinguir mayúsculas ni tildes

#### Scenario: Filtro por estado
- **WHEN** el usuario filtra por "disponible"
- **THEN** la lista muestra solo las casas disponibles

### Requirement: Editar y eliminar casas
El sistema SHALL permitir editar cualquier dato de una casa y eliminarla previa confirmación. Al eliminar una casa, SHALL eliminar también sus valores de características.

#### Scenario: Eliminar una casa
- **WHEN** el usuario elimina una casa y confirma
- **THEN** la casa y sus valores de características desaparecen y la casa ya no aparece en la lista

#### Scenario: Cancelar la eliminación
- **WHEN** el usuario pulsa eliminar y luego cancela
- **THEN** la casa no se modifica
