#include "documents/formats.hpp"

#include <fstream>
#include <set>
#include <stdexcept>

#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace wcatalog {

FormatCatalog FormatCatalog::load(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("No se encontró el archivo de formatos: " + to_utf8(file));
    Json doc = Json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_array()) {
        throw std::runtime_error("El archivo de formatos no es una lista JSON válida: " + to_utf8(file));
    }

    FormatCatalog catalog;
    std::set<std::string> seen;
    for (const Json& item : doc) {
        if (!item.is_object()) throw std::runtime_error("Formato inválido en " + to_utf8(file));
        DocFormat f;
        f.id = item.value("id", "");
        f.name = item.value("nombre", "");
        f.width = item.value("ancho", 0);
        f.height = item.value("alto", 0);
        f.max_badges = item.value("max_badges", 8);
        f.description_lines = item.value("lineas_descripcion", 6);
        const bool id_ok = !f.id.empty() && f.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
        if (!id_ok || f.name.empty() || f.width <= 0 || f.height <= 0 || f.max_badges < 0 || f.description_lines < 1) {
            throw std::runtime_error("Formato inválido en " + to_utf8(file) + ": " + item.dump());
        }
        if (!seen.insert(f.id).second) throw std::runtime_error("Formato repetido: " + f.id);
        catalog.formats_.push_back(std::move(f));
    }
    if (catalog.formats_.empty()) throw std::runtime_error("No hay formatos definidos en " + to_utf8(file));
    return catalog;
}

std::optional<DocFormat> FormatCatalog::find(const std::string& id) const {
    for (const DocFormat& f : formats_) {
        if (f.id == id) return f;
    }
    return std::nullopt;
}

Json FormatCatalog::to_json() const {
    Json out = Json::array();
    for (const DocFormat& f : formats_) {
        out.push_back(Json{{"id", f.id},
                           {"nombre", f.name},
                           {"ancho", f.width},
                           {"alto", f.height},
                           {"max_badges", f.max_badges},
                           {"lineas_descripcion", f.description_lines}});
    }
    return out;
}

}  // namespace wcatalog
