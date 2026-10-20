# Spec Delta

## Purpose

Define el servidor HTTP local que comunica la interfaz con el núcleo de la aplicación: quién puede acceder, cómo son las respuestas y errores, y qué endpoints base existen.

## ADDED Requirements

### Requirement: Acceso solo desde la máquina local
El sistema SHALL aceptar conexiones únicamente en la interfaz de loopback (`127.0.0.1`). SHALL rechazar las peticiones cuyo encabezado `Host` no corresponda a la dirección y el puerto del propio servidor.

#### Scenario: Acceso desde otra máquina
- **WHEN** otro equipo de la red intenta conectarse al puerto de la aplicación
- **THEN** la conexión no es posible, porque el servidor no escucha fuera de loopback

#### Scenario: Encabezado Host ajeno
- **WHEN** llega una petición con un encabezado `Host` distinto de `127.0.0.1:<puerto>` o `localhost:<puerto>`
- **THEN** el servidor responde 403 con un error JSON y no ejecuta la operación

### Requirement: Formato común de respuestas JSON
Las rutas bajo `/api/` SHALL responder con JSON codificado en UTF-8. Los errores SHALL tener la forma `{"error": {"code": "<codigo>", "message": "<mensaje legible>"}}` y un código de estado HTTP coherente: 400 para validación, 404 para no encontrado, 409 para conflicto y 500 para error interno.

#### Scenario: Ruta de API inexistente
- **WHEN** la interfaz solicita una ruta `/api/...` que no existe
- **THEN** el servidor responde 404 con un cuerpo de error JSON con `code` igual a `not_found`

#### Scenario: Error interno
- **WHEN** ocurre una excepción no controlada al atender una petición
- **THEN** el servidor responde 500 con un error JSON, registra el detalle en el log y sigue atendiendo peticiones

#### Scenario: Texto con acentos
- **WHEN** una respuesta contiene texto con tildes o ñ
- **THEN** el texto llega a la interfaz sin alteraciones

### Requirement: Endpoints de salud e información
El sistema SHALL exponer `GET /api/health`, que responde `{"status": "ok"}`, y `GET /api/info`, que responde la versión de la aplicación, la ruta de la carpeta de datos y la versión del esquema de la base de datos.

#### Scenario: Consulta de información
- **WHEN** la interfaz solicita `GET /api/info`
- **THEN** recibe un JSON con los campos `version`, `dataDir` y `schemaVersion`

### Requirement: Entrega de la interfaz web
El sistema SHALL servir los archivos de la interfaz (HTML, CSS, JS e imágenes) con el tipo de contenido correcto. Solicitar `/` SHALL devolver la página principal.

#### Scenario: Carga inicial
- **WHEN** la ventana de la aplicación abre la URL raíz del servidor
- **THEN** recibe la página principal y sus recursos, y la interfaz se muestra sin errores

#### Scenario: Archivo inexistente
- **WHEN** se solicita un archivo estático que no existe
- **THEN** el servidor responde 404
