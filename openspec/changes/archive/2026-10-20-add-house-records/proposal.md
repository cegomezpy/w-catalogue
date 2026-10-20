# Proposal

## Why

La casa es la unidad central de todo flyer o catálogo. El usuario necesita registrar sus casas con unos pocos datos fijos (cuartos, comedores independientes, ubicación, etc.) y con un conjunto amplio y extensible de características opcionales. Algunas de ellas no pueden faltar en el modelo: gas permanente, respaldo energético, internet y seguridad. Cada característica debe poder mostrarse como un badge personalizado.

## What Changes

- **Registro de casas** (crear, ver, editar, eliminar, listar y buscar). Solo el título es obligatorio.
- **Datos fijos de la casa:**
  - Título, descripción y operación (venta/alquiler).
  - Estado: disponible, reservada, vendida o alquilada.
  - Precio opcional con moneda, y qué mostrar si no hay precio: "Consultar" u ocultarlo.
  - Cuartos, baños, comedores independientes, área construida, área de terreno y niveles.
  - Ubicación: dirección, barrio/sector, municipio, provincia y coordenadas opcionales. Se pueden pegar desde Google Maps.
- **Catálogo de características extensible:**
  - Cada característica tiene etiqueta, categoría, tipo de valor (sí/no, número, texto, opción única, opción múltiple), opciones, unidad e icono.
  - Cada una tiene su configuración de badge: texto con plantilla `{valor}`, rol de color (primario/secundario/terciario) y si se muestra o no.
- **Características predefinidas** al primer arranque:
  - Servicios: gas permanente, respaldo energético, internet, seguridad, agua y climatización.
  - Espacios: patio/jardín, terraza, balcón, parqueos, cuarto de servicio, lavandería y piscina.
  - Condición: amueblada, estado de conservación y año de construcción.
  - Las predefinidas se pueden editar y desactivar, pero no eliminar.
- **Características propias del usuario**, que puede crear, editar y eliminar.
- **Badges resueltos** por casa: la API devuelve la lista de badges que corresponde a los valores de cada casa, lista para usar en documentos.
- **Interfaz:**
  - Lista de casas con búsqueda y filtro por estado.
  - Formulario de casa por secciones.
  - Administración de características en Ajustes.

## Capabilities

### New Capabilities
- `house-records`: registro de casas con sus datos fijos, precio opcional, estado, ubicación con coordenadas opcionales, listado y búsqueda.
- `house-features`: catálogo extensible de características con tipos de valor, características predefinidas, asignación de valores a casas, configuración de badges y resolución de badges por casa.

### Modified Capabilities
<!-- Ninguna. -->

## Impact

- **Depende de:**
  - `setup-app-foundation`.
  - `add-catalog-settings`: la moneda por defecto y los roles de color usados en los badges.
- **Base de datos:** nueva migración con las tablas `casa`, `caracteristica` y `casa_caracteristica`, y los datos semilla de las características predefinidas.
- **API:**
  - `GET/POST /api/casas`
  - `GET/PUT/DELETE /api/casas/{id}`
  - `GET/POST /api/caracteristicas`
  - `PUT/DELETE /api/caracteristicas/{id}`
- **Interfaz:** páginas `#/casas`, `#/casas/{id}` y `#/ajustes/caracteristicas`.
- **Lo consumirán:** `add-house-photos` (fotos por casa) y `add-document-export` (datos y badges en los documentos).
