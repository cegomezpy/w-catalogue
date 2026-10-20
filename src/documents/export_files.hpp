#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "documents/document_context.hpp"

namespace wcatalog {

// Nombre de la carpeta de una exportación: <AAAA-MM-DD_HHMM>_<slug>_<flyer|catalogo>.
// `stamp` es la fecha y hora ya formateada ("2026-10-20_1435"); el título se pasa a slug sin tildes.
std::string output_folder_name(const std::string& stamp, const std::string& title, const std::string& type);

// Fecha y hora local actual con la forma "AAAA-MM-DD_HHMM".
std::string current_stamp();

// Crea la carpeta de salida bajo `outputs_dir` con ese nombre. Si ya existe se añade -2, -3...:
// nunca se reutiliza ni se modifica una exportación anterior. Lanza std::runtime_error si no se puede crear.
std::filesystem::path create_output_folder(const std::filesystem::path& outputs_dir, const std::string& name);

// Copia a <folder>/assets/ los archivos que usa el HTML (fotos, portada y fuentes). Un mismo destino
// repetido se copia una sola vez. Lanza std::runtime_error con la ruta si un origen falta o no se puede copiar.
void copy_assets(const std::filesystem::path& folder, const std::vector<AssetFile>& assets);

// Escribe un archivo de texto UTF-8. Lanza std::runtime_error si falla.
void write_text_file(const std::filesystem::path& file, const std::string& content);

}  // namespace wcatalog
