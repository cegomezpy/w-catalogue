#include "db/migration_sql.hpp"

namespace wcatalog {

// Migración 3: casas, catálogo de características y valores por casa, con las 16 características
// predefinidas. Las claves (`clave`) son estables; el orden sigue el del spec house-features.
const char* const kMigrationHouses = R"sql(
CREATE TABLE casa (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  titulo TEXT NOT NULL CHECK (length(trim(titulo)) > 0),
  descripcion TEXT NOT NULL DEFAULT '',
  operacion TEXT CHECK (operacion IS NULL OR operacion IN ('venta', 'alquiler')),
  estado TEXT NOT NULL DEFAULT 'disponible'
    CHECK (estado IN ('disponible', 'reservada', 'vendida', 'alquilada')),
  precio_centavos INTEGER CHECK (precio_centavos IS NULL OR precio_centavos >= 0),
  moneda TEXT,
  sin_precio TEXT NOT NULL DEFAULT 'consultar' CHECK (sin_precio IN ('consultar', 'ocultar')),
  cuartos INTEGER CHECK (cuartos IS NULL OR cuartos >= 0),
  banos INTEGER CHECK (banos IS NULL OR banos >= 0),
  comedores_independientes INTEGER CHECK (comedores_independientes IS NULL OR comedores_independientes >= 0),
  niveles INTEGER CHECK (niveles IS NULL OR niveles >= 0),
  area_construida_m2 REAL CHECK (area_construida_m2 IS NULL OR area_construida_m2 >= 0),
  area_terreno_m2 REAL CHECK (area_terreno_m2 IS NULL OR area_terreno_m2 >= 0),
  direccion TEXT NOT NULL DEFAULT '',
  barrio TEXT NOT NULL DEFAULT '',
  municipio TEXT NOT NULL DEFAULT '',
  provincia TEXT NOT NULL DEFAULT '',
  lat REAL CHECK (lat IS NULL OR (lat >= -90 AND lat <= 90)),
  lon REAL CHECK (lon IS NULL OR (lon >= -180 AND lon <= 180)),
  texto_busqueda TEXT NOT NULL DEFAULT '',
  creado TEXT NOT NULL,
  actualizado TEXT NOT NULL,
  CHECK ((lat IS NULL) = (lon IS NULL))
);
CREATE INDEX idx_casa_actualizado ON casa (actualizado);
CREATE INDEX idx_casa_estado ON casa (estado);
CREATE INDEX idx_casa_texto_busqueda ON casa (texto_busqueda);

CREATE TABLE caracteristica (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  clave TEXT NOT NULL UNIQUE,
  etiqueta TEXT NOT NULL CHECK (length(trim(etiqueta)) > 0),
  categoria TEXT NOT NULL CHECK (categoria IN ('servicios', 'espacios', 'condicion', 'otra')),
  tipo TEXT NOT NULL CHECK (tipo IN ('bool', 'numero', 'texto', 'opcion', 'multiopcion')),
  opciones TEXT NOT NULL DEFAULT '[]',
  unidad TEXT NOT NULL DEFAULT '',
  icono TEXT NOT NULL DEFAULT '',
  badge_visible INTEGER NOT NULL DEFAULT 1 CHECK (badge_visible IN (0, 1)),
  badge_texto TEXT,
  badge_rol TEXT NOT NULL DEFAULT 'secundario' CHECK (badge_rol IN ('primario', 'secundario', 'terciario')),
  orden INTEGER NOT NULL,
  activa INTEGER NOT NULL DEFAULT 1 CHECK (activa IN (0, 1)),
  predefinida INTEGER NOT NULL DEFAULT 0 CHECK (predefinida IN (0, 1))
);

CREATE TABLE casa_caracteristica (
  casa_id INTEGER NOT NULL REFERENCES casa (id) ON DELETE CASCADE,
  caracteristica_id INTEGER NOT NULL REFERENCES caracteristica (id) ON DELETE CASCADE,
  valor TEXT NOT NULL,
  PRIMARY KEY (casa_id, caracteristica_id)
);
CREATE INDEX idx_casa_caracteristica_car ON casa_caracteristica (caracteristica_id);

INSERT INTO caracteristica (clave, etiqueta, categoria, tipo, opciones, icono, badge_rol, orden, predefinida) VALUES
 ('gas_permanente', 'Gas permanente', 'servicios', 'bool', '[]', 'flame', 'secundario', 1, 1),
 ('respaldo_energetico', 'Respaldo energético', 'servicios', 'multiopcion',
  '["Planta eléctrica","Paneles solares","Inversor con baterías","Otro"]', 'zap', 'secundario', 2, 1),
 ('internet', 'Internet', 'servicios', 'opcion', '["Fibra","ADSL","Datos móviles","Otro"]', 'wifi', 'secundario', 3, 1),
 ('seguridad', 'Seguridad', 'servicios', 'multiopcion',
  '["Rejas","Cámaras","Alarma","Vigilancia","Cerca perimetral"]', 'shield', 'secundario', 4, 1),
 ('agua', 'Agua', 'servicios', 'multiopcion',
  '["Suministro permanente","Cisterna","Tanque elevado","Pozo"]', 'droplets', 'secundario', 5, 1),
 ('climatizacion', 'Climatización', 'servicios', 'multiopcion',
  '["Aire acondicionado","Ventiladores de techo"]', 'snowflake', 'secundario', 6, 1),
 ('patio_jardin', 'Patio o jardín', 'espacios', 'bool', '[]', 'trees', 'primario', 7, 1),
 ('terraza', 'Terraza', 'espacios', 'bool', '[]', 'sun', 'primario', 8, 1),
 ('balcon', 'Balcón', 'espacios', 'bool', '[]', 'door-open', 'primario', 9, 1),
 ('cuarto_servicio', 'Cuarto de servicio', 'espacios', 'bool', '[]', 'bed-single', 'primario', 10, 1),
 ('lavanderia', 'Lavandería', 'espacios', 'bool', '[]', 'washing-machine', 'primario', 11, 1),
 ('piscina', 'Piscina', 'espacios', 'bool', '[]', 'waves-ladder', 'primario', 12, 1),
 ('parqueos', 'Parqueos', 'espacios', 'numero', '[]', 'car', 'primario', 13, 1),
 ('amueblada', 'Amueblada', 'condicion', 'opcion', '["Sí","Parcialmente","No"]', 'sofa', 'terciario', 14, 1),
 ('estado_conservacion', 'Estado de conservación', 'condicion', 'opcion',
  '["Nueva","Excelente","Buena","A remodelar"]', 'sparkles', 'terciario', 15, 1),
 ('anio_construccion', 'Año de construcción', 'condicion', 'numero', '[]', 'calendar', 'terciario', 16, 1);

UPDATE caracteristica SET badge_visible = 0 WHERE clave = 'anio_construccion';
)sql";

}  // namespace wcatalog
