#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "db/database.hpp"
#include "fonts/font_metadata.hpp"
#include "settings/settings_service.hpp"

namespace wcatalog {

struct FontPaths {
    std::filesystem::path included_dir;  // fonts/ junto al exe (con fuentes.json)
    std::filesystem::path imported_dir;  // data/fuentes/
    std::filesystem::path tmp_dir;       // data/tmp/
};

struct FontVariant {
    std::int64_t id = 0;  // 0 para las incluidas (no se pueden eliminar)
    std::string family;
    int weight = 400;
    std::string style = "normal";  // normal | italic
    std::string origin;            // incluida | importada
    std::string file;
    std::string format = "ttf";
    std::string generic = "sans-serif";
};

// Biblioteca unificada: fuentes incluidas (manifiesto) e importadas (base de datos + data/fuentes/).
class FontLibrary {
public:
    FontLibrary(Database& db, SettingsService& settings, FontPaths paths);

    // Lee fonts/fuentes.json. Lanza std::runtime_error si falta o está dañado.
    void load_manifest();

    std::vector<FontVariant> variants() const;
    std::vector<FontVariant> family_variants(const std::string& family) const;
    bool family_exists(const std::string& family) const;

    // [{familia, origen, variantes: [{id, peso, estilo, origen, archivo, formato, url}]}]
    nlohmann::json families_json() const;
    static nlohmann::json variant_json(const FontVariant& v);

    // Paso 1 de la importación: valida, guarda en data/tmp/ y devuelve metadatos y token.
    nlohmann::json analyze(const std::string& bytes, const std::string& original_name);

    // Paso 2: {token, familia, peso, estilo, reemplazar}. Devuelve la variante creada.
    nlohmann::json import_font(const nlohmann::json& request);

    // Elimina una variante importada. Devuelve {"avisos": [...]}.
    nlohmann::json remove(std::int64_t id);

    // Borra temporales con más de `max_age_hours` horas.
    void cleanup_tmp(int max_age_hours = 1);

    // Ruta de un archivo servible (origen "incluidas"|"importadas"); vacía si no existe.
    std::filesystem::path file_path(const std::string& origin, const std::string& name) const;

private:
    std::vector<FontVariant> imported_variants() const;
    static std::string slug(const std::string& s);

    Database& db_;
    SettingsService& settings_;
    FontPaths paths_;
    std::vector<FontVariant> included_;
};

}  // namespace wcatalog
