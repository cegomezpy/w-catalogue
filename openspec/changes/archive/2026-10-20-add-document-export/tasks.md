# Tasks

## 1. Dependencias, esquema y formatos

- [x] 1.1 Vendorizar Inja en `third_party/` y registrar su versión y licencia; verificar con un test que renderiza una plantilla con `if`, `for` e `include`
- [x] 1.2 Añadir la migración de `documento` y `documento_casa` con sus claves foráneas (cascada en `casa`, `SET NULL` en la foto de portada); verificar con un test que se aplica sobre la base con fotos
- [x] 1.3 Crear `templates/formatos.json` con los tres formatos (incluido `max_badges`), su carga y `GET /api/formatos`, y configurar `cmake --install` para copiar `templates/`; verificar con un test que la API devuelve los tres formatos con sus dimensiones

## 2. Documentos

- [x] 2.1 Implementar el repositorio y el servicio de documentos: flyer (una casa, título y descripción heredados o propios), catálogo (título obligatorio, al menos una casa, orden, descripción corta), y listar, editar y eliminar; verificar con tests de cada escenario de `catalog-documents`
- [x] 2.2 Implementar la portada: subida (reutilizando el pipeline de fotos hacia `data/documentos/<id>/`) o elección de una foto de una casa incluida; verificar con tests de ambas vías y de una foto de portada eliminada (`SET NULL`)
- [x] 2.3 Implementar `GET /api/casas/{id}/documentos` y la eliminación de flyers sin casa dentro de la transacción de eliminación de una casa; verificar con un test del escenario "casa usada en un flyer y un catálogo"
- [x] 2.4 Implementar los endpoints `/api/documentos` y `POST /api/documentos/{id}/portada`; verificar con tests HTTP de alta de flyer, alta de catálogo sin casas (400) y reordenación

## 3. Contexto y render

- [x] 3.1 Implementar el constructor de contexto (D3): `precio_texto`, descripción corta automática (180 caracteres sin cortar palabras, con "…"), `mapa_url`, badges limitados a `max_badges` con `badges_restantes`, fotos con `src` según el modo, y tema CSS con la URL base del modo; verificar con tests de casa mínima, precio oculto, precio "Consultar", 14 badges con máximo 8 y recorte de descripción
- [x] 3.2 Escribir `base.css` (reglas de página, `print-color-adjust`, `.no-print`, recorte de líneas) y las plantillas básicas `flyer.html` y `catalogo.html` con sus parciales y bloques condicionales; verificar renderizando un flyer de casa mínima y uno completo, comprobando que no hay secciones vacías en el HTML
- [x] 3.3 Implementar `GET /render/{id}/{formato}` en modo `preview` con un error claro ante fallos de plantilla (archivo y línea); verificar con un test HTTP y con una plantilla rota a propósito

## 4. Exportación

- [x] 4.1 Implementar la carpeta de salida (slug sin tildes, fecha-hora y sufijo ante colisión) y la copia de assets (fotos usadas, portada y fuentes elegidas); verificar con tests de nombre, colisión y contenido de `assets/`
- [x] 4.2 Implementar la escritura de los HTML en modo `export` (CSS en línea, rutas relativas y barra "Guardar como PDF") y la limpieza de la carpeta si hay un error antes de que exista ningún HTML; verificar con un test que el HTML no contiene referencias a `/media/` ni a `http://127.0.0.1`
- [x] 4.3 Implementar el generador de PDF con Edge headless (perfil `edge-headless`, `CREATE_NO_WINDOW`, tiempo máximo por páginas, validación `%PDF` y motivos de fallo); verificar con un test de integración que genera un PDF de 1080×1350 (810×1012.5 pt) a partir de un HTML de prueba, y con un test que simula Edge ausente y devuelve `edge_no_encontrado`
- [x] 4.4 Implementar `POST /api/documentos/{id}/exportar` con el mutex (409 `exportacion_en_curso`), la instantánea, `resultado.json` y la respuesta por formato; verificar con tests HTTP de exportación completa, de exportación con Edge ausente (HTML presentes y PDF fallido) y de dos peticiones simultáneas
- [x] 4.5 Implementar `POST /api/sistema/abrir-ruta` restringido a `data/salidas/`; verificar con un test que una ruta fuera de `salidas` devuelve 400, y manualmente que se abre el Explorador

## 5. Interfaz

- [x] 5.1 Crear `#/documentos` con la lista y los botones "Nuevo flyer" (con selector de casa) y "Nuevo catálogo", y el botón "Crear flyer" en el formulario de casa; verificar manualmente creando ambos tipos
- [x] 5.2 Crear el editor `#/documentos/{id}`: título y descripción (con indicación de "heredado de la casa" en flyers), casas ordenables con descripción corta, y portada subida o elegida; verificar manualmente con un catálogo de 3 casas
- [x] 5.3 Crear la vista previa con `<iframe>` escalado y el selector de formato; verificar manualmente que al cambiar un color en Ajustes la vista previa lo refleja
- [x] 5.4 Crear el panel Exportar (casillas de formatos, indicador de progreso y resultado por formato con Abrir carpeta / HTML / PDF, y la indicación de "Guardar como PDF" cuando el PDF falla), y el aviso de documentos afectados al eliminar una casa; verificar manualmente una exportación completa y una con Edge forzado como ausente

## 6. Verificación integrada

- [x] 6.1 De punta a punta con fotos reales: exportar un flyer y un catálogo de 3 casas en los tres formatos; comprobar el tamaño de página de cada PDF, las fuentes incrustadas, el texto seleccionable, el peso razonable (registrar los MB) y el tiempo total
- [x] 6.2 Prueba del respaldo: con Edge forzado como ausente, exportar; abrir el HTML con la app cerrada y sin red, usar "Guardar como PDF" en Edge y comprobar que el PDF manual tiene el tamaño correcto, con fondos y sin la barra
- [x] 6.3 Instantánea: exportar, cambiar el precio y el color primario, y comprobar que los archivos exportados no cambian y que la vista previa sí
