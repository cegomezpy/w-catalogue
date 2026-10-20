# Proposal

## Why

Todos los flyers y catálogos deben salir con la identidad visual del usuario: su tipografía y sus tres colores. También deben llevar sus datos de contacto. Esa configuración tiene que existir antes de crear casas y exportar documentos, porque ambos la consumen. Además, el usuario necesita poder usar fuentes que no vienen con la aplicación.

## What Changes

- Pantalla de **Ajustes** con:
  - **Colores:** primario, secundario y terciario, con valores por defecto y validación.
  - **Tipografía:** una fuente para títulos y otra para texto, elegidas de la biblioteca de fuentes.
  - **Contacto:** nombre del negocio o agente, teléfono, WhatsApp, email, sitio web e Instagram/Facebook. Todo opcional.
  - **Moneda por defecto**, que se usa al crear casas.
- **Biblioteca de fuentes:**
  - Fuentes libres (licencia OFL) incluidas con la aplicación.
  - Importación de archivos `.ttf`/`.otf`, con lectura automática del nombre de familia, el peso y el estilo.
  - Vista previa con texto en español.
  - Eliminación de fuentes importadas.
- Un botón que abre Google Fonts en el navegador predeterminado para que el usuario descargue fuentes y luego las importe. La descarga automática queda fuera de alcance.
- **Tema como CSS:** el sistema publica la configuración actual como variables CSS y `@font-face`. Lo consumen la vista previa y la exportación.

## Capabilities

### New Capabilities
- `catalog-settings`: colores del tema, elección de fuentes de títulos y texto, datos de contacto, moneda por defecto y publicación del tema como CSS.
- `font-library`: fuentes incluidas, importación, validación, vista previa y eliminación de fuentes.

### Modified Capabilities
<!-- Ninguna. -->

## Impact

- **Depende de:** `setup-app-foundation` (servidor, base de datos, shell de la interfaz y carpeta `data/`).
- **Base de datos:** nueva migración con las tablas `ajustes` y `fuente`.
- **API:**
  - `GET/PUT /api/ajustes`
  - `GET/POST /api/fuentes`
  - `DELETE /api/fuentes/{id}`
  - `GET /api/tema.css`
  - `GET /media/fuentes/...`
  - `POST /api/sistema/abrir-enlace`
- **Archivos:**
  - `fonts/` junto al exe (fuentes incluidas y su `fuentes.json`)
  - `data/fuentes/` (fuentes importadas)
- **Dependencia nueva:** `stb_truetype` (dominio público/MIT), para leer los metadatos de las fuentes.
- **Lo consumirán:** `add-house-records` (moneda por defecto y roles de color de los badges) y `add-document-export` (tema y contacto).
