#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wcatalog {

// Formato de salida (templates/formatos.json). Las medidas son píxeles CSS.
struct DocFormat {
    std::string id;
    std::string name;
    int width = 0;
    int height = 0;
    int max_badges = 8;
    int description_lines = 6;
};

// Catálogo de formatos leído de un archivo de datos, para añadir uno sin recompilar.
class FormatCatalog {
public:
    // Lee el archivo. Lanza std::runtime_error con el motivo si falta o es inválido.
    static FormatCatalog load(const std::filesystem::path& file);

    const std::vector<DocFormat>& all() const { return formats_; }
    std::optional<DocFormat> find(const std::string& id) const;

    // [{id, nombre, ancho, alto, max_badges, lineas_descripcion}]
    nlohmann::json to_json() const;

private:
    std::vector<DocFormat> formats_;
};

}  // namespace wcatalog
