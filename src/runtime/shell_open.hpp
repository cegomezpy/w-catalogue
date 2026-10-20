#pragma once

#include <filesystem>

namespace wcatalog {

// Abre un archivo con la aplicación predeterminada de Windows, o una carpeta en el Explorador.
bool open_path_in_shell(const std::filesystem::path& path);

// ¿`path` está dentro de `base` (o es `base`)? Compara rutas ya resueltas (sin "..", enlaces ni
// diferencias de mayúsculas). Ambas deben existir; si no, devuelve false.
bool is_path_within(const std::filesystem::path& base, const std::filesystem::path& path);

}  // namespace wcatalog
