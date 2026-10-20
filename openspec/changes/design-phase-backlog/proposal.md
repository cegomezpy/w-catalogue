# Proposal

> **Estado: BORRADOR / BACKLOG.** Este change solo guarda el trabajo pospuesto para la fase de diseño. Todavía no tiene specs, design ni tasks. Se completa cuando terminen los 5 changes fundacionales, y entonces probablemente se divida en varios changes más pequeños.

## Why

Los changes fundacionales (`setup-app-foundation`, `add-catalog-settings`, `add-house-records`, `add-house-photos` y `add-document-export`) dejan la aplicación funcionando con plantillas neutras. Varias mejoras visuales y de salida se pospusieron a propósito para sacar antes una versión usable. Este documento las reúne para no perderlas.

## What Changes

Pendientes de la fase de diseño:

- **Exportar PNG para Instagram.** Instagram no acepta PDF. Hay que generar una imagen por página (1080×1350 / 1080×1920), con captura de Edge headless y el mismo respaldo que el PDF: si falla, queda el HTML.
- **QR y mapa.**
  - QR offline hacia las coordenadas en Google Maps, con la biblioteca C/C++ de Nayuki (MIT).
  - Evaluar un mapa estático: requiere red, así que sería opcional.
- **Logo del negocio** en Ajustes, para la portada del catálogo y el pie de los flyers.
- **Revisión de contraste** entre los colores del tema y el texto, con un aviso si un color deja el texto ilegible (WCAG).
- **Diseño visual definitivo de las plantillas** en `templates/`:
  - Flyer "moderno": foto a sangre, chips sobre la foto, franja de datos clave con iconos y badges.
  - Composición por formato.
  - Catálogo con varias casas por página.
  - Presentación de iconos y badges.
- **Formato de los números:** `85,000` frente a `85.000`, y la posición del símbolo de moneda. Es la única pregunta abierta de los designs. Se resuelve en el filtro de plantilla de precio (`add-document-export`, design D3).

Otras ideas pospuestas en los designs, para valorar en la misma fase o después:

- HTML exportado en un **solo archivo** (imágenes y fuentes en base64) para compartirlo fácilmente.
- **Descarga automática de fuentes** desde Google Fonts. Hoy se hace con un enlace y la importación manual.
- **Varios temas con nombre** y la posibilidad de elegir tema por documento.
- **Renombrar una opción** de característica actualizando en bloque las casas que la usan.
- **Exportación en segundo plano** con progreso por formato, para catálogos grandes.
- Ventana propia con **WebView2** en lugar de `msedge --app`. Requiere MSVC.
- Firma de código del ejecutable, para evitar avisos de SmartScreen.

## Capabilities

<!-- Por definir al completar el change. Previsiblemente:
     Modified: document-export, catalog-documents, catalog-settings, house-features
     New:      image-export (PNG) -->

### New Capabilities
- Por definir.

### Modified Capabilities
- Por definir.

## Impact

- Sobre todo afecta `templates/` (plantillas, CSS y `formatos.json`), que se editan sin recompilar.
- También afecta la pantalla de Ajustes (logo y contraste) y el pipeline de exportación (PNG y QR).
- **Requisito previo:** tener aplicados los 5 changes fundacionales.
