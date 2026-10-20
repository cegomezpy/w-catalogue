# Design

## Context

Se construye sobre `setup-app-foundation` (capas http → servicios → repositorios, SQLite con migraciones, Alpine.js) y `add-catalog-settings` (moneda por defecto y roles de color). La casa combina pocos datos fijos, que todos los documentos usan siempre, con muchas características opcionales que varían por casa y que el usuario puede ampliar sin tocar código.

## Goals / Non-Goals

**Goals:**
- Un modelo en el que añadir una característica nueva sea un registro, no una migración.
- Badges calculados en el servidor, para que la vista previa y la exportación muestren lo mismo.
- Que una búsqueda sin tildes encuentre texto con tildes ("vedado" encuentra "Vedado", "pena" encuentra "Peña").

**Non-Goals:**
- Fotos: llegan en `add-house-photos`.
- Aspecto visual final de los badges e iconos en los documentos: queda para la fase de diseño.
- Otros tipos de inmueble distintos de la casa, importación desde Excel/CSV y duplicar casas.

## Decisions

### D1. Modelo híbrido: columnas fijas + EAV tipado
```
casa                          caracteristica                    casa_caracteristica
----                          --------------                    -------------------
id                            id, clave (UNIQUE)                casa_id     -> casa (CASCADE)
titulo NOT NULL               etiqueta, categoria               caracteristica_id -> (CASCADE)
descripcion                   tipo: bool|numero|texto|          valor TEXT NOT NULL
operacion  (venta|alquiler)         opcion|multiopcion          PK(casa_id, caracteristica_id)
estado     (disponible|...)   opciones  (JSON array)
precio_centavos INTEGER NULL  unidad, icono
moneda, sin_precio            badge_visible, badge_texto NULL,
cuartos, banos,               badge_rol (primario|secundario|
comedores_independientes,                terciario)
niveles,                      orden, activa, predefinida
area_construida_m2,
area_terreno_m2,
direccion, barrio, municipio,
provincia, lat, lon,
texto_busqueda,
creado, actualizado
```
- **Columnas fijas:** los datos que el usuario marcó como imprescindibles y que los documentos usan en posiciones fijas (franja de datos clave, ubicación y precio).
- **EAV tipado:** las características extensibles. El `tipo` define cómo se valida y se serializa `valor`:
  - `bool`: `"1"`/`"0"`.
  - `numero`: decimal en texto.
  - `opcion`: el texto de la opción.
  - `multiopcion`: un arreglo JSON.
- *Alternativas descartadas:*
  - Una columna JSON por casa: se pierden la validación por característica y las consultas de "cuántas casas la usan".
  - Columnas fijas para todo: cada característica nueva exigiría una migración.
- Gas permanente, respaldo energético, internet y seguridad son características **predefinidas** y no columnas fijas. Así obtienen badges configurables y el usuario puede editarlas, pero no eliminarlas (`predefinida = 1`).

### D2. Precio en centavos
`precio_centavos INTEGER NULL` evita errores de coma flotante. La API recibe y devuelve `precio` como número decimal, y el servicio convierte redondeando a 2 decimales. `sin_precio` (`consultar` | `ocultar`, con `consultar` por defecto) aplica solo cuando no hay precio.

### D3. Búsqueda sin tildes
SQLite `lower()` solo trata ASCII. El servicio guarda `texto_busqueda` con título, barrio y municipio en minúsculas y sin diacríticos, mediante una función de plegado UTF-8 propia para el rango latino. La consulta se normaliza igual y se usa `LIKE`.
- *Alternativa:* FTS5 o ICU. Es excesivo para unos cientos de casas.

### D4. Reglas de integridad de características
- No se puede cambiar el `tipo` de una característica que ya tiene valores en alguna casa: se responde 409 con el número de casas afectadas.
- No se puede quitar de `opciones` una opción que usa alguna casa: se responde 409 indicando qué opción y cuántas casas la usan. El usuario debe cambiar esas casas primero. Renombrar una opción equivale a quitarla y añadir otra, así que aplica la misma regla.
- Las características predefinidas no se pueden eliminar (409), pero sí desactivar.
- Al eliminar una característica propia, el borrado se propaga en cascada a sus valores. La interfaz pide confirmación con el recuento que devuelve `GET /api/caracteristicas/{id}` (campo `casas_con_valor`).

### D5. API
- `GET /api/casas?q=&estado=` devuelve un resumen por casa: id, título, estado, operación, precio, moneda, barrio, municipio, cuartos, baños y actualizado.
- `GET /api/casas/{id}` devuelve la casa completa más `caracteristicas: [{caracteristica_id, valor}]` y `badges: [{caracteristica_id, texto, rol, icono}]`.
- `POST /api/casas` y `PUT /api/casas/{id}` reciben la casa completa con sus `caracteristicas`, y el `PUT` las reemplaza en una sola transacción. Los errores 400 incluyen `fields: {campo: mensaje}` para mostrarlos junto a cada campo.
- `GET/POST /api/caracteristicas`, `GET/PUT/DELETE /api/caracteristicas/{id}`, y `PUT /api/caracteristicas/orden` para reordenar.

### D6. Resolución de badges en el servicio
Una función pura recibe los valores de la casa y el catálogo, y devuelve los badges:
1. Filtra las características `activa`, con `badge_visible` y con valor. En las `bool`, solo las que valen `"1"`.
2. Ordena por `orden`.
3. Sustituye `{valor}` en `badge_texto`, o usa el texto por defecto: la etiqueta en las `bool`, y `etiqueta: valor` en las demás.
4. Une las `multiopcion` con ", " y añade la `unidad` a los números si existe.

Se cubre con tests unitarios. La exportación usará exactamente esta función.

### D7. Datos semilla
La migración inserta las 16 predefinidas con `clave` estable (`gas_permanente`, `respaldo_energetico`, `internet`, `seguridad`, `agua`, `climatizacion`, `patio_jardin`, `terraza`, `balcon`, `parqueos`, `cuarto_servicio`, `lavanderia`, `piscina`, `amueblada`, `estado_conservacion`, `anio_construccion`).
- **Roles por defecto:** servicios con rol `secundario`, espacios con `primario` y condición con `terciario`.
- `anio_construccion` se crea con `badge_visible = 0`.
- Las propias del usuario reciben una `clave` generada a partir de la etiqueta, con un sufijo si se repite.

### D8. Coordenadas en la interfaz
El formulario tiene un campo "Coordenadas" que acepta `lat, lon` y también URLs de Google Maps con `@lat,lon`. La conversión se hace en `web/js/coords.js` y se envían `lat` y `lon` numéricos. El servidor valida los rangos y que estén ambos o ninguno.

### D9. Interfaz
- `#/casas` es la lista con búsqueda (con debounce) y filtro por estado.
- `#/casas/nueva` y `#/casas/{id}` son el formulario por secciones: Básico, Precio, Distribución, Ubicación y Características. Las características se agrupan por categoría, con un control por tipo: casilla, número, texto, selector y grupo de casillas.
- `#/ajustes/caracteristicas` es la administración del catálogo: lista ordenable, editor con vista previa del badge usando los colores del tema, y opciones para desactivar o eliminar.
- Los iconos se guardan como nombre. La interfaz ofrece un subconjunto de iconos Lucide (licencia ISC) vendorizado en `web/vendor/icons/`. Su presentación en los documentos se define en la fase de diseño.

## Risks / Trade-offs

- **[EAV más complejo de consultar]** → El volumen es pequeño. Las consultas pasan por el repositorio y la función de badges tiene tests.
- **[Renombrar opciones obliga a editar casas]** → Es preferible a corromper valores en silencio. Si resulta molesto, se puede añadir más adelante una acción "renombrar opción" que actualice los valores en bloque.
- **[Plegado de tildes propio]** → Cubre solo el rango latino, que basta para español. Tiene tests con á, é, í, ó, ú, ü y ñ.

## Migration Plan

Se añade la siguiente migración disponible, que crea las tres tablas, los índices (`casa.actualizado`, `casa.estado`, `casa.texto_busqueda`) y las 16 características predefinidas.
