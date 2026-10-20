#include "fonts/font_library.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <tuple>

#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

using Json = nlohmann::json;
using Kind = FontError::Kind;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

void write_file(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw FontError(Kind::Invalid, "No se pudo guardar el archivo en " + to_utf8(p));
}

bool is_hex_token(const std::string& t) {
    return t.size() == 32 && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string new_token() {
    static const char* digits = "0123456789abcdef";
    std::random_device rd;
    std::string t;
    for (int i = 0; i < 32; ++i) t += digits[rd() % 16];
    return t;
}

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

// La familia acaba dentro de CSS entre comillas: sin comillas, barras ni caracteres de control.
bool family_name_ok(const std::string& f) {
    if (f.empty() || utf8_length(f) > 100) return false;
    for (unsigned char c : f) {
        if (c < 0x20 || c == 0x7F || c == '"' || c == '\'' || c == '\\' || c == '<' || c == '>' || c == '{' ||
            c == '}' || c == ';') {
            return false;
        }
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

FontLibrary::FontLibrary(Database& db, SettingsService& settings, FontPaths paths)
    : db_(db), settings_(settings), paths_(std::move(paths)) {}

void FontLibrary::load_manifest() {
    const fs::path manifest = paths_.included_dir / "fuentes.json";
    std::ifstream in(manifest, std::ios::binary);
    if (!in) throw std::runtime_error("No se encontró el manifiesto de fuentes: " + to_utf8(manifest));
    Json doc = Json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.contains("fuentes") || !doc["fuentes"].is_array()) {
        throw std::runtime_error("El manifiesto de fuentes está dañado: " + to_utf8(manifest));
    }
    included_.clear();
    for (const Json& f : doc["fuentes"]) {
        FontVariant v;
        v.family = f.value("familia", "");
        v.file = f.value("archivo", "");
        v.weight = f.value("peso", 400);
        v.style = f.value("estilo", "normal");
        v.generic = f.value("generica", "sans-serif");
        v.origin = "incluida";
        v.format = fs::path(v.file).extension() == ".otf" ? "otf" : "ttf";
        if (v.family.empty() || v.file.empty()) continue;
        included_.push_back(std::move(v));
    }
}

std::vector<FontVariant> FontLibrary::imported_variants() const {
    std::vector<FontVariant> out;
    Statement st = db_.prepare("SELECT id, familia, peso, estilo, archivo, formato FROM fuente ORDER BY familia, peso, estilo");
    while (st.step()) {
        FontVariant v;
        v.id = st.column_int(0);
        v.family = st.column_text(1);
        v.weight = static_cast<int>(st.column_int(2));
        v.style = st.column_text(3);
        v.file = st.column_text(4);
        v.format = st.column_text(5);
        v.origin = "importada";
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<FontVariant> FontLibrary::variants() const {
    std::vector<FontVariant> all = included_;
    auto imp = imported_variants();
    all.insert(all.end(), imp.begin(), imp.end());
    return all;
}

std::vector<FontVariant> FontLibrary::family_variants(const std::string& family) const {
    std::vector<FontVariant> out;
    for (auto& v : variants()) {
        if (v.family == family) out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](const FontVariant& a, const FontVariant& b) {
        return std::tie(a.weight, a.style) < std::tie(b.weight, b.style);
    });
    return out;
}

bool FontLibrary::family_exists(const std::string& family) const { return !family_variants(family).empty(); }

Json FontLibrary::variant_json(const FontVariant& v) {
    const std::string dir = v.origin == "incluida" ? "incluidas" : "importadas";
    return Json{{"id", v.id == 0 ? Json(nullptr) : Json(v.id)},
                {"peso", v.weight},
                {"estilo", v.style},
                {"origen", v.origin},
                {"archivo", v.file},
                {"formato", v.format},
                {"url", "/media/fuentes/" + dir + "/" + v.file}};
}

Json FontLibrary::families_json() const {
    std::map<std::string, std::vector<FontVariant>> by_family;
    for (auto& v : variants()) by_family[v.family].push_back(std::move(v));

    Json out = Json::array();
    for (auto& [family, vs] : by_family) {
        std::sort(vs.begin(), vs.end(), [](const FontVariant& a, const FontVariant& b) {
            return std::tie(a.weight, a.style) < std::tie(b.weight, b.style);
        });
        bool included = std::any_of(vs.begin(), vs.end(), [](const FontVariant& v) { return v.origin == "incluida"; });
        Json variantes = Json::array();
        for (const auto& v : vs) variantes.push_back(variant_json(v));
        out.push_back(Json{{"familia", family}, {"origen", included ? "incluida" : "importada"}, {"variantes", variantes}});
    }
    return out;
}

std::string FontLibrary::slug(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) && c < 0x80) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "fuente" : out;
}

Json FontLibrary::analyze(const std::string& bytes, const std::string& original_name) {
    FontMetadata md = read_font_metadata(bytes);  // valida; lanza FontError(Invalid)

    std::error_code ec;
    fs::create_directories(paths_.tmp_dir, ec);
    const std::string token = new_token();
    write_file(paths_.tmp_dir / (token + "." + md.format), bytes);

    Json warnings = Json::array();
    if (md.embedding_restricted()) {
        warnings.push_back(
            "La licencia de esta fuente restringe su incrustación en PDF. Confirme que puede usarla en sus documentos.");
    }
    return Json{{"token", token},
                {"archivoOriginal", original_name},
                {"familia", md.family},
                {"subfamilia", md.subfamily},
                {"peso", md.weight},
                {"estilo", md.italic ? "italic" : "normal"},
                {"formato", md.format},
                {"avisos", warnings}};
}

Json FontLibrary::import_font(const Json& req) {
    const std::string token = req.value("token", "");
    if (!is_hex_token(token)) throw FontError(Kind::Invalid, "El token de importación no es válido");

    fs::path tmp;
    std::string format;
    for (const char* ext : {"ttf", "otf"}) {
        fs::path p = paths_.tmp_dir / (token + "." + ext);
        if (fs::exists(p)) {
            tmp = p;
            format = ext;
        }
    }
    if (tmp.empty()) throw FontError(Kind::NotFound, "La importación expiró; vuelva a seleccionar el archivo");

    const std::string family = trim(req.value("familia", ""));
    const int weight = req.contains("peso") && req["peso"].is_number_integer() ? req["peso"].get<int>() : 0;
    const std::string style = req.value("estilo", "");
    const bool replace = req.value("reemplazar", false);

    if (!family_name_ok(family)) {
        throw FontError(Kind::Invalid,
                        "Nombre de familia no válido (máximo 100 caracteres, sin comillas ni símbolos de código)");
    }
    if (weight < 100 || weight > 900 || weight % 100 != 0) {
        throw FontError(Kind::Invalid, "El peso debe ser un múltiplo de 100 entre 100 y 900");
    }
    if (style != "normal" && style != "italic") throw FontError(Kind::Invalid, "El estilo debe ser normal o italic");

    // la variante ya viene incluida: no se puede pisar
    for (const auto& v : included_) {
        if (v.family == family && v.weight == weight && v.style == style) {
            throw FontError(Kind::Conflict, "Esa variante de \"" + family + "\" ya viene incluida con la aplicación");
        }
    }

    // revalidar el archivo (por si el temporal se alteró) antes de aceptarlo
    read_font_metadata(read_file(tmp));

    std::int64_t existing_id = 0;
    std::string existing_file;
    {
        Statement st = db_.prepare("SELECT id, archivo FROM fuente WHERE familia = ? AND peso = ? AND estilo = ?");
        st.bind(1, family).bind(2, std::int64_t{weight}).bind(3, style);
        if (st.step()) {
            existing_id = st.column_int(0);
            existing_file = st.column_text(1);
        }
    }
    if (existing_id != 0 && !replace) {
        throw FontError(Kind::Conflict, "Ya existe la variante " + family + " " + std::to_string(weight) + " " + style +
                                            "; confirme si desea reemplazarla");
    }

    std::error_code ec;
    fs::create_directories(paths_.imported_dir, ec);
    const std::string base = slug(family) + "-" + std::to_string(weight) + "-" + style;
    std::string file = base + "." + format;
    for (int n = 2; fs::exists(paths_.imported_dir / file) && file != existing_file; ++n) {
        file = base + "-" + std::to_string(n) + "." + format;
    }
    const fs::path dest = paths_.imported_dir / file;

    // Se copia primero el archivo y luego se registra; si el registro falla se borra la copia.
    fs::copy_file(tmp, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) throw FontError(Kind::Invalid, "No se pudo guardar la fuente en " + to_utf8(dest));

    std::int64_t id = existing_id;
    try {
        Transaction tx(db_);
        if (existing_id != 0) {
            Statement up = db_.prepare("UPDATE fuente SET archivo = ?, formato = ? WHERE id = ?");
            up.bind(1, file).bind(2, format).bind(3, existing_id);
            up.step();
        } else {
            Statement ins = db_.prepare("INSERT INTO fuente (familia, peso, estilo, archivo, formato) VALUES (?, ?, ?, ?, ?)");
            ins.bind(1, family).bind(2, std::int64_t{weight}).bind(3, style).bind(4, file).bind(5, format);
            ins.step();
            id = db_.last_insert_rowid();
        }
        tx.commit();
    } catch (...) {
        fs::remove(dest, ec);
        throw;
    }

    // el archivo anterior de una variante reemplazada ya no se usa
    if (existing_id != 0 && existing_file != file) fs::remove(paths_.imported_dir / existing_file, ec);
    fs::remove(tmp, ec);

    log().info("Fuente importada: " + family + " " + std::to_string(weight) + " " + style);
    FontVariant v;
    v.id = id;
    v.family = family;
    v.weight = weight;
    v.style = style;
    v.origin = "importada";
    v.file = file;
    v.format = format;
    return variant_json(v);
}

Json FontLibrary::remove(std::int64_t id) {
    std::string family, file;
    {
        Statement st = db_.prepare("SELECT familia, archivo FROM fuente WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw FontError(Kind::NotFound, "La fuente no existe");
        family = st.column_text(0);
        file = st.column_text(1);
    }

    Json warnings = Json::array();
    {
        Transaction tx(db_);
        Statement del = db_.prepare("DELETE FROM fuente WHERE id = ?");
        del.bind(1, id);
        del.step();

        // si la familia desapareció y estaba elegida, los ajustes vuelven a la fuente por defecto
        if (!family_exists(family)) {
            struct Role { const char* key; const char* label; };
            for (const Role& r : {Role{SettingsService::kFuenteTitulos, "títulos"},
                                  Role{SettingsService::kFuenteTexto, "texto"}}) {
                if (settings_.value(r.key) == family) {
                    settings_.reset(r.key);
                    warnings.push_back("La fuente de " + std::string(r.label) + " \"" + family +
                                       "\" se eliminó; se restableció a \"" + SettingsService::default_value(r.key) + "\".");
                }
            }
        }
        tx.commit();
    }

    std::error_code ec;
    fs::remove(paths_.imported_dir / file, ec);
    log().info("Fuente eliminada: " + family + " (" + file + ")");
    return Json{{"avisos", warnings}};
}

void FontLibrary::cleanup_tmp(int max_age_hours) {
    std::error_code ec;
    if (!fs::is_directory(paths_.tmp_dir, ec)) return;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(max_age_hours);
    for (const auto& e : fs::directory_iterator(paths_.tmp_dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::error_code ec2;
        if (e.last_write_time(ec2) < limit) fs::remove(e.path(), ec2);
    }
}

fs::path FontLibrary::file_path(const std::string& origin, const std::string& name) const {
    const bool included = origin == "incluidas";
    if (!included && origin != "importadas") return {};
    for (const auto& v : variants()) {
        if (v.file == name && (v.origin == "incluida") == included) {
            fs::path p = (included ? paths_.included_dir : paths_.imported_dir) / name;
            std::error_code ec;
            return fs::is_regular_file(p, ec) ? p : fs::path{};
        }
    }
    return {};
}

}  // namespace wcatalog
