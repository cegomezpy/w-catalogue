#pragma once

#include <filesystem>
#include <string>

namespace wcatalog {

class Database;

// Datos compartidos que necesitan los manejadores de rutas.
struct AppContext {
    std::string version;
    std::filesystem::path data_dir;
    Database* db = nullptr;
};

}  // namespace wcatalog
