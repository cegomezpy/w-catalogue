#pragma once

#include <filesystem>
#include <string>

namespace wcatalog {

struct AppPaths {
    std::filesystem::path exe_dir;
    std::filesystem::path data_dir;
    std::filesystem::path logs_dir;
    std::filesystem::path backups_dir;
    std::filesystem::path fonts_dir;  // data/fuentes: fuentes importadas
    std::filesystem::path photos_dir; // data/fotos/<casa_id>: fotos de las casas
    std::filesystem::path tmp_dir;    // data/tmp: temporales (importaciones a medias)
    std::filesystem::path documents_dir;  // data/documentos/<id>: portadas subidas
    std::filesystem::path outputs_dir;    // data/salidas: carpetas de exportación
};

// Carpeta que contiene el ejecutable en ejecución.
std::filesystem::path current_exe_dir();

// Calcula las rutas a partir de la carpeta del ejecutable, sin tocar el disco.
AppPaths make_paths(const std::filesystem::path& exe_dir);

// Crea data/, logs/ y backups/ si faltan y comprueba que se puede escribir.
// Lanza std::runtime_error con la ruta afectada si no es posible.
void ensure_data_dirs(const AppPaths& paths);

// Carpeta de la interfaz: web/ junto al exe (distribución) o en el nivel superior
// (compilación en build/, para iterar sin copiar). Si no existe ninguna, la primera.
std::filesystem::path resolve_web_dir(const std::filesystem::path& exe_dir);

// Carpeta de las fuentes incluidas: fonts/ junto al exe o en el nivel superior (build/).
std::filesystem::path resolve_included_fonts_dir(const std::filesystem::path& exe_dir);

// Carpeta de plantillas: templates/ junto al exe o en el nivel superior (build/).
std::filesystem::path resolve_templates_dir(const std::filesystem::path& exe_dir);

// Ruta en UTF-8, para JSON, logs y mensajes.
std::string to_utf8(const std::filesystem::path& p);

}  // namespace wcatalog
