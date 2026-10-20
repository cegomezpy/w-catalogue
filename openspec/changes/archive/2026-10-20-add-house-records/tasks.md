# Tasks

## 1. Esquema y datos semilla

- [x] 1.1 Añadir la migración con las tablas `casa`, `caracteristica` y `casa_caracteristica`, sus restricciones `CHECK`, las claves foráneas en cascada y los índices; verificar con un test que la migración se aplica sobre una base con los ajustes ya creados
- [x] 1.2 Insertar en la misma migración las 16 características predefinidas con sus tipos, opciones, categorías y configuración de badge (D7); verificar con un test que existen todas, con `predefinida = 1`, y que `anio_construccion` tiene el badge oculto

## 2. Utilidades de dominio

- [x] 2.1 Implementar el plegado de texto para búsqueda (minúsculas y sin diacríticos en UTF-8); verificar con tests de á, é, í, ó, ú, ü, ñ y mayúsculas acentuadas
- [x] 2.2 Implementar la conversión del precio decimal a centavos y viceversa; verificar con tests de redondeo (85000, 0.005, 1234.5) y del rechazo de negativos
- [x] 2.3 Implementar la validación y serialización de valores por tipo de característica; verificar con tests de cada tipo, de una opción inexistente y de un número negativo

## 3. Características

- [x] 3.1 Implementar el repositorio y el servicio de características: crear (con `clave` generada), editar, reordenar, desactivar y eliminar, con las reglas de D4 (sin eliminar predefinidas, sin cambiar el tipo con valores, sin quitar opciones en uso); verificar con tests de cada regla y sus 409
- [x] 3.2 Implementar la resolución de badges (D6); verificar con tests de los escenarios del spec: sí/no, opción única, opción múltiple, número con unidad, plantilla `{valor}`, badge oculto, característica inactiva y valor "no"
- [x] 3.3 Implementar los endpoints `/api/caracteristicas` (incluido `casas_con_valor` y `PUT /orden`); verificar con tests HTTP de alta, edición, reordenación y eliminación en uso

## 4. Casas

- [x] 4.1 Implementar el repositorio y el servicio de casas: validación (título obligatorio, enteros no negativos, coordenadas completas y en rango, operación, estado y `sin_precio`), moneda por defecto tomada de los ajustes, `texto_busqueda` y marcas de tiempo; verificar con tests de cada regla de validación del spec
- [x] 4.2 Implementar el guardado de valores de características en la misma transacción que la casa (reemplazo completo en `PUT`); verificar con un test que un valor inválido revierte todo el guardado
- [x] 4.3 Implementar los endpoints `/api/casas` (lista con `q` y `estado`, detalle con `caracteristicas` y `badges`, alta, edición y eliminación en cascada) con errores `fields`; verificar con tests HTTP de creación mínima, búsqueda "vedado" y "pena", filtro por estado y eliminación

## 5. Interfaz de casas

- [x] 5.1 Crear `#/casas` con la lista, la búsqueda con debounce, el filtro por estado y el botón "Nueva casa"; verificar manualmente con varias casas que la búsqueda sin tildes y el filtro funcionan
- [x] 5.2 Crear el formulario de casa con las secciones Básico, Precio (moneda propuesta y opción sin precio), Distribución y Ubicación, con los errores mostrados por campo; verificar manualmente creando una casa solo con título y otra completa
- [x] 5.3 Implementar `web/js/coords.js` (`lat, lon` y URL de Google Maps con `@lat,lon`) e integrarlo en el campo Coordenadas; verificar manualmente pegando ambos formatos y un valor inválido
- [x] 5.4 Crear la sección Características del formulario, agrupada por categoría con un control por tipo, y una vista previa de los badges resueltos; verificar manualmente con el escenario de Gas permanente + Fibra + Cámaras/Rejas
- [x] 5.5 Implementar la eliminación de una casa con diálogo de confirmación; verificar manualmente que cancelar no cambia nada y que confirmar la quita de la lista

## 6. Interfaz de características

- [x] 6.1 Vendorizar el subconjunto de iconos Lucide (ISC) en `web/vendor/icons/` y registrar su licencia; verificar que el selector de iconos los muestra
- [x] 6.2 Crear `#/ajustes/caracteristicas`: lista ordenable, editor (etiqueta, categoría, tipo, opciones, unidad, icono y badge con vista previa en los colores del tema), desactivar y eliminar con el recuento de casas afectadas; verificar manualmente creando "Vista al mar", usándola en una casa y eliminándola

## 7. Verificación integrada

- [x] 7.1 De punta a punta: crear una casa con todos los datos fijos, coordenadas pegadas desde Google Maps, sin precio y con 5 características; reabrir la app y comprobar que los datos y los badges de `GET /api/casas/{id}` coinciden con lo introducido
