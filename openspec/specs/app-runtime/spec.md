# app-runtime Specification

## Purpose

Define cómo se arranca, se muestra, se cierra y guarda sus datos la aplicación de escritorio portable para Windows, de modo que funcione al copiar su carpeta a otra máquina.

## Requirements

### Requirement: Ejecutable portable sin instalación
El sistema SHALL ejecutarse en Windows 11 x64 a partir de la carpeta de distribución copiada, sin instalador y sin runtimes adicionales. La única dependencia externa es Microsoft Edge, que viene incluido en el sistema.

#### Scenario: Copia a otra máquina
- **WHEN** el usuario copia la carpeta de distribución a otra máquina con Windows 11 y hace doble clic en `WCatalog.exe`
- **THEN** la aplicación arranca y muestra su interfaz sin pedir instalar nada

#### Scenario: Sin ventana de consola
- **WHEN** la aplicación arranca
- **THEN** no aparece ninguna ventana de consola, solo la ventana de la interfaz

### Requirement: Carpeta de datos portable
El sistema SHALL guardar todos los datos del usuario en una carpeta `data/` situada junto al ejecutable, y SHALL crearla con su estructura si no existe.

#### Scenario: Primer arranque
- **WHEN** la aplicación arranca y no existe `data/` junto al ejecutable
- **THEN** crea `data/`, la base de datos y la carpeta de logs, y arranca con normalidad

#### Scenario: Los datos viajan con la carpeta
- **WHEN** el usuario copia la carpeta completa (ejecutable y `data/`) a otra ubicación o máquina y la ejecuta
- **THEN** la aplicación muestra los mismos datos que tenía en la ubicación original

#### Scenario: Carpeta sin permisos de escritura
- **WHEN** la aplicación no puede crear o escribir en `data/`
- **THEN** muestra un mensaje de error que indica la ruta afectada y termina sin dejar procesos en ejecución

### Requirement: Ventana de la aplicación
El sistema SHALL abrir la interfaz en una ventana de Microsoft Edge en modo aplicación, sin barra de direcciones ni pestañas. Si Edge no se encuentra, SHALL abrir la interfaz en el navegador predeterminado.

#### Scenario: Edge disponible
- **WHEN** la aplicación arranca en una máquina con Edge instalado
- **THEN** se abre una ventana sin controles de navegador que muestra la página de inicio de la aplicación

#### Scenario: Edge no disponible
- **WHEN** la aplicación arranca y no encuentra Edge
- **THEN** abre la interfaz en el navegador predeterminado y la aplicación funciona igual

### Requirement: Cierre de la aplicación
El sistema SHALL terminar su proceso cuando el usuario cierra la ventana de la interfaz y no queda ninguna otra ventana de la aplicación abierta. Antes de terminar, SHALL cerrar la base de datos de forma ordenada.

#### Scenario: Cierre de la ventana
- **WHEN** el usuario cierra la única ventana de la aplicación
- **THEN** el proceso `WCatalog.exe` termina en un máximo de 15 segundos

#### Scenario: Recarga de la página
- **WHEN** el usuario recarga la interfaz (F5)
- **THEN** la aplicación sigue en ejecución y la interfaz vuelve a cargar

### Requirement: Instancia única por carpeta de datos
El sistema SHALL ejecutar como máximo una instancia por carpeta de datos. Un segundo arranque SHALL abrir una ventana conectada a la instancia existente en lugar de iniciar otra.

#### Scenario: Segundo doble clic
- **WHEN** la aplicación ya está en ejecución y el usuario vuelve a ejecutar `WCatalog.exe` desde la misma carpeta
- **THEN** se abre una ventana de la interfaz conectada a la instancia existente y no se crea un segundo servidor

### Requirement: Base de datos local con migraciones
El sistema SHALL guardar sus datos en una base SQLite dentro de `data/` y SHALL actualizar su esquema automáticamente al arrancar. Antes de aplicar migraciones sobre una base existente, SHALL hacer una copia de seguridad.

#### Scenario: Actualización de versión
- **WHEN** una versión nueva de la aplicación arranca con una base de datos de una versión anterior
- **THEN** guarda una copia de la base en `data/backups/` y aplica las migraciones pendientes sin perder datos

#### Scenario: Migración fallida
- **WHEN** una migración falla
- **THEN** la base queda en el estado previo a esa migración, el error se registra y se muestra al usuario

#### Scenario: Base de una versión más nueva
- **WHEN** la base de datos tiene una versión de esquema mayor que la que conoce la aplicación
- **THEN** la aplicación no modifica la base, informa que debe usarse una versión más reciente y termina

### Requirement: Errores fatales visibles y registro
El sistema SHALL registrar sus eventos y errores en un archivo de log dentro de `data/logs/`. Cualquier error que impida arrancar SHALL mostrarse al usuario en un cuadro de diálogo.

#### Scenario: Error de arranque
- **WHEN** ocurre un error que impide arrancar el servidor
- **THEN** se muestra un cuadro de diálogo con la descripción del error y el error queda registrado en el log
