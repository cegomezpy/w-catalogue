#include "documents/document_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <random>
#include <set>

#include "documents/document_text.hpp"
#include "imaging/photo_pipeline.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

const char* const kNow = "strftime('%Y-%m-%dT%H:%M:%fZ', 'now')";

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string random_name() {
    static std::mutex m;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(m);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(rng()));
    return buf;
}

void write_file(const fs::path& file, const std::uint8_t* data, std::size_t size) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error("No se pudo escribir el archivo: " + to_utf8(file));
}

struct TmpDirGuard {
    fs::path dir;
    ~TmpDirGuard() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void bind_opt_text(Statement& st, int i, const std::optional<std::string>& v) {
    if (v) st.bind(i, *v);
    else st.bind_null(i);
}

}  // namespace

struct DocumentService::Parsed {
    std::optional<std::string> title, description;
    std::int64_t house_id = 0;  // flyer
    struct Entry {
        std::int64_t house_id = 0;
        std::optional<std::string> short_description;
    };
    std::vector<Entry> houses;  // catálogo
    Json formats = Json::array();
    bool has_formats = false;
};

DocumentService::DocumentService(Database& db, const FormatCatalog& formats, DocumentPaths paths)
    : db_(db), formats_(formats), paths_(std::move(paths)) {}

fs::path DocumentService::doc_dir(std::int64_t id) const { return paths_.documents_dir / std::to_string(id); }

std::string DocumentService::type_of(std::int64_t id) const {
    Statement st = db_.prepare("SELECT tipo FROM documento WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) throw NotFoundError("El documento no existe");
    return st.column_text(0);
}

DocumentService::Parsed DocumentService::parse(const Json& payload, bool creating, const std::string& type) const {
    std::map<std::string, std::string> errors;
    Parsed p;
    if (!payload.is_object()) throw ValidationFailed({{"", "Se esperaba un objeto JSON"}});

    // texto opcional: ausente, null o vacío -> sin valor propio
    auto optional_text = [&](const char* key, std::size_t max_chars, std::optional<std::string>& target) {
        if (!payload.contains(key) || payload[key].is_null()) return;
        if (!payload[key].is_string()) {
            errors[key] = "Debe ser un texto";
            return;
        }
        const std::string v = trim(payload[key].get<std::string>());
        if (utf8_length(v) > max_chars) errors[key] = "Máximo " + std::to_string(max_chars) + " caracteres";
        else if (!v.empty()) target = v;
    };
    optional_text("titulo", kMaxDocumentTitleChars, p.title);
    optional_text("descripcion", kMaxDocumentDescriptionChars, p.description);

    auto existing_house = [&](std::int64_t id) {
        Statement st = db_.prepare("SELECT 1 FROM casa WHERE id = ?");
        st.bind(1, id);
        return st.step();
    };

    if (type == "flyer") {
        if (creating) {
            if (!payload.contains("casa_id") || !payload["casa_id"].is_number_integer()) {
                errors["casa_id"] = "Elija la casa del flyer";
            } else {
                p.house_id = payload["casa_id"].get<std::int64_t>();
                if (!existing_house(p.house_id)) errors["casa_id"] = "La casa no existe";
            }
        }
    } else {
        if (!p.title && !errors.count("titulo")) errors["titulo"] = "El título del catálogo es obligatorio";
        const bool has_list = payload.contains("casas") && payload["casas"].is_array();
        if (!has_list || payload["casas"].empty()) {
            errors["casas"] = "Hace falta al menos una casa";
        } else if (payload["casas"].size() > static_cast<std::size_t>(kMaxHousesPerCatalog)) {
            errors["casas"] = "Un catálogo admite como máximo " + std::to_string(kMaxHousesPerCatalog) + " casas";
        } else {
            std::set<std::int64_t> seen;
            for (const Json& item : payload["casas"]) {
                if (!item.is_object() || !item.contains("casa_id") || !item["casa_id"].is_number_integer()) {
                    errors["casas"] = "Cada casa necesita un casa_id entero";
                    break;
                }
                Parsed::Entry e;
                e.house_id = item["casa_id"].get<std::int64_t>();
                if (!existing_house(e.house_id)) {
                    errors["casas"] = "La casa " + std::to_string(e.house_id) + " no existe";
                    break;
                }
                if (!seen.insert(e.house_id).second) {
                    errors["casas"] = "Una casa está repetida en el catálogo";
                    break;
                }
                if (item.contains("descripcion_corta") && !item["descripcion_corta"].is_null()) {
                    if (!item["descripcion_corta"].is_string()) {
                        errors["casas"] = "La descripción corta debe ser un texto";
                        break;
                    }
                    const std::string v = trim(item["descripcion_corta"].get<std::string>());
                    if (utf8_length(v) > static_cast<std::size_t>(kMaxShortDescriptionChars)) {
                        errors["casas"] = "La descripción corta admite como máximo " +
                                          std::to_string(kMaxShortDescriptionChars) + " caracteres";
                        break;
                    }
                    if (!v.empty()) e.short_description = v;
                }
                p.houses.push_back(std::move(e));
            }
        }
    }

    if (payload.contains("formatos_preferidos") && !payload["formatos_preferidos"].is_null()) {
        const Json& f = payload["formatos_preferidos"];
        if (!f.is_array()) {
            errors["formatos_preferidos"] = "Debe ser una lista de formatos";
        } else {
            for (const Json& id : f) {
                if (!id.is_string() || !formats_.find(id.get<std::string>())) {
                    errors["formatos_preferidos"] = "Formato desconocido";
                    break;
                }
            }
            if (!errors.count("formatos_preferidos")) {
                p.formats = f;
                p.has_formats = true;
            }
        }
    }

    if (!errors.empty()) throw ValidationFailed(std::move(errors));
    return p;
}

void DocumentService::write_houses(std::int64_t doc_id, const Parsed& p) {
    {
        Statement del = db_.prepare("DELETE FROM documento_casa WHERE documento_id = ?");
        del.bind(1, doc_id);
        del.step();
    }
    Statement ins = db_.prepare(
        "INSERT INTO documento_casa (documento_id, casa_id, orden, descripcion_corta) VALUES (?, ?, ?, ?)");
    std::int64_t order = 0;
    for (const Parsed::Entry& e : p.houses) {
        ins.reset();
        ins.bind(1, doc_id).bind(2, e.house_id).bind(3, ++order);
        bind_opt_text(ins, 4, e.short_description);
        ins.step();
    }
}

Json DocumentService::create(const Json& payload) {
    if (!payload.is_object() || !payload.contains("tipo") || !payload["tipo"].is_string() ||
        (payload["tipo"] != "flyer" && payload["tipo"] != "catalogo")) {
        throw ValidationFailed({{"tipo", "El tipo debe ser flyer o catalogo"}});
    }
    const std::string type = payload["tipo"].get<std::string>();
    Parsed p = parse(payload, true, type);

    std::int64_t id = 0;
    {
        Transaction tx(db_);
        Statement ins = db_.prepare(std::string("INSERT INTO documento (tipo, titulo, descripcion, formatos_preferidos, "
                                                "creado, actualizado) VALUES (?, ?, ?, ?, ") + kNow + ", " + kNow + ")");
        ins.bind(1, type);
        bind_opt_text(ins, 2, p.title);
        bind_opt_text(ins, 3, p.description);
        ins.bind(4, p.formats.dump());
        ins.step();
        id = db_.last_insert_rowid();
        if (type == "flyer") p.houses.push_back({p.house_id, std::nullopt});
        write_houses(id, p);
        tx.commit();
    }
    return get(id);
}

Json DocumentService::update(std::int64_t id, const Json& payload) {
    const std::string type = type_of(id);
    Parsed p = parse(payload, false, type);

    Transaction tx(db_);
    {
        Statement up = db_.prepare(std::string("UPDATE documento SET titulo = ?, descripcion = ?, "
                                               "formatos_preferidos = COALESCE(?, formatos_preferidos), actualizado = ") +
                                   kNow + " WHERE id = ?");
        bind_opt_text(up, 1, p.title);
        bind_opt_text(up, 2, p.description);
        if (p.has_formats) up.bind(3, p.formats.dump());
        else up.bind_null(3);
        up.bind(4, id);
        up.step();
    }
    if (type == "catalogo") {
        write_houses(id, p);
        drop_cover_if_invalid(id);
    }
    tx.commit();
    return get(id);
}

// La portada elegida entre las fotos de las casas deja de valer si su casa sale del catálogo.
void DocumentService::drop_cover_if_invalid(std::int64_t doc_id) {
    Statement up = db_.prepare(
        "UPDATE documento SET portada_foto_id = NULL WHERE id = ? AND portada_foto_id IS NOT NULL AND "
        "portada_foto_id NOT IN (SELECT f.id FROM foto f JOIN documento_casa dc ON dc.casa_id = f.casa_id "
        "WHERE dc.documento_id = ?)");
    up.bind(1, doc_id).bind(2, doc_id);
    up.step();
}

void DocumentService::remove_cover_files(std::int64_t id) const {
    std::error_code ec;
    fs::remove_all(doc_dir(id), ec);
    if (ec) log().warn("No se pudo borrar la carpeta del documento " + std::to_string(id) + ": " + ec.message());
}

void DocumentService::remove(std::int64_t id) {
    {
        Statement st = db_.prepare("DELETE FROM documento WHERE id = ?");
        st.bind(1, id);
        st.step();
    }
    if (db_.changes() == 0) throw NotFoundError("El documento no existe");
    remove_cover_files(id);
}

Json DocumentService::get(std::int64_t id) const {
    Json doc;
    std::int64_t cover_photo = 0;
    bool own_cover = false;
    {
        Statement st = db_.prepare(
            "SELECT id, tipo, titulo, descripcion, portada_foto_id, portada_propia, formatos_preferidos, creado, "
            "actualizado FROM documento WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw NotFoundError("El documento no existe");
        doc = Json{{"id", st.column_int(0)},
                   {"tipo", st.column_text(1)},
                   {"titulo", st.is_null(2) ? Json(nullptr) : Json(st.column_text(2))},
                   {"descripcion", st.is_null(3) ? Json(nullptr) : Json(st.column_text(3))},
                   {"formatos_preferidos", Json::parse(st.column_text(6), nullptr, false)},
                   {"creado", st.column_text(7)},
                   {"actualizado", st.column_text(8)}};
        if (doc["formatos_preferidos"].is_discarded()) doc["formatos_preferidos"] = Json::array();
        cover_photo = st.is_null(4) ? 0 : st.column_int(4);
        own_cover = st.column_int(5) != 0;
    }

    Json houses = Json::array();
    std::string first_title, first_description;
    {
        Statement st = db_.prepare(
            "SELECT dc.casa_id, c.titulo, c.descripcion, dc.orden, dc.descripcion_corta FROM documento_casa dc "
            "JOIN casa c ON c.id = dc.casa_id WHERE dc.documento_id = ? ORDER BY dc.orden, dc.casa_id");
        st.bind(1, id);
        while (st.step()) {
            const std::string description = st.column_text(2);
            const bool has_short = !st.is_null(4);
            if (houses.empty()) {
                first_title = st.column_text(1);
                first_description = description;
            }
            houses.push_back(Json{{"casa_id", st.column_int(0)},
                                  {"titulo", st.column_text(1)},
                                  {"orden", st.column_int(3)},
                                  {"descripcion_corta", has_short ? Json(st.column_text(4)) : Json(nullptr)},
                                  {"descripcion_corta_efectiva",
                                   has_short ? st.column_text(4) : make_short_description(description)}});
        }
    }
    doc["casas"] = houses;

    const bool is_flyer = doc["tipo"] == "flyer";
    doc["titulo_efectivo"] = !doc["titulo"].is_null() ? doc["titulo"] : Json(is_flyer ? first_title : "");
    doc["descripcion_efectiva"] =
        !doc["descripcion"].is_null() ? doc["descripcion"] : Json(is_flyer ? first_description : "");

    doc["portada"] = nullptr;
    if (own_cover) {
        doc["portada"] = Json{{"origen", "subida"},
                              {"foto_id", nullptr},
                              {"url", "/media/documentos/" + std::to_string(id) + "/portada_opt.jpg"}};
    } else if (cover_photo != 0) {
        Statement st = db_.prepare("SELECT casa_id FROM foto WHERE id = ?");
        st.bind(1, cover_photo);
        if (st.step()) {
            doc["portada"] = Json{{"origen", "casa"},
                                  {"foto_id", cover_photo},
                                  {"url", "/media/fotos/" + std::to_string(st.column_int(0)) + "/" +
                                              std::to_string(cover_photo) + "_opt.jpg"}};
        }
    }
    return doc;
}

Json DocumentService::list() const {
    Json out = Json::array();
    Statement st = db_.prepare(
        "SELECT d.id, d.tipo, COALESCE(d.titulo, (SELECT c.titulo FROM documento_casa dc JOIN casa c ON c.id = dc.casa_id "
        "WHERE dc.documento_id = d.id ORDER BY dc.orden LIMIT 1), ''), "
        "(SELECT COUNT(*) FROM documento_casa WHERE documento_id = d.id), d.actualizado "
        "FROM documento d ORDER BY d.actualizado DESC, d.id DESC");
    while (st.step()) {
        out.push_back(Json{{"id", st.column_int(0)},
                           {"tipo", st.column_text(1)},
                           {"titulo", st.column_text(2)},
                           {"casas", st.column_int(3)},
                           {"actualizado", st.column_text(4)}});
    }
    return out;
}

Json DocumentService::for_house(std::int64_t house_id) const {
    Json out = Json::array();
    Statement st = db_.prepare(
        "SELECT d.id, d.tipo, COALESCE(d.titulo, (SELECT c.titulo FROM casa c WHERE c.id = ?), '') FROM documento d "
        "JOIN documento_casa dc ON dc.documento_id = d.id WHERE dc.casa_id = ? ORDER BY d.actualizado DESC, d.id DESC");
    st.bind(1, house_id).bind(2, house_id);
    while (st.step()) {
        const std::string type = st.column_text(1);
        out.push_back(Json{{"id", st.column_int(0)},
                           {"tipo", type},
                           {"titulo", st.column_text(2)},
                           {"efecto", type == "flyer" ? "se_elimina" : "se_quita"}});
    }
    return out;
}

Json DocumentService::set_cover_upload(std::int64_t id, const std::uint8_t* data, std::size_t size) {
    if (type_of(id) != "catalogo") throw ValidationFailed({{"portada", "Solo los catálogos tienen portada"}});

    // decodificar y redimensionar es lo pesado: se hace sin tener la base bloqueada
    const ProcessedPhoto processed = process_photo(data, size);

    std::error_code ec;
    fs::create_directories(paths_.tmp_dir, ec);
    TmpDirGuard tmp{paths_.tmp_dir / ("portada-" + random_name())};
    fs::create_directories(tmp.dir, ec);
    if (ec) throw std::runtime_error("No se pudo crear la carpeta temporal: " + to_utf8(tmp.dir));
    write_file(tmp.dir / ("orig." + processed.original_ext), data, size);
    write_file(tmp.dir / "opt.jpg", processed.opt_jpeg.data(), processed.opt_jpeg.size());
    write_file(tmp.dir / "min.jpg", processed.min_jpeg.data(), processed.min_jpeg.size());

    Transaction tx(db_);
    type_of(id);  // sigue existiendo
    const fs::path dest = doc_dir(id);
    fs::create_directories(dest, ec);
    if (ec) throw std::runtime_error("No se pudo crear la carpeta del documento: " + to_utf8(dest));
    // una portada anterior de otro formato no debe quedar huérfana
    for (const char* old : {"portada_orig.jpg", "portada_orig.png"}) fs::remove(dest / old, ec);
    fs::rename(tmp.dir / ("orig." + processed.original_ext), dest / ("portada_orig." + processed.original_ext));
    fs::rename(tmp.dir / "opt.jpg", dest / "portada_opt.jpg");
    fs::rename(tmp.dir / "min.jpg", dest / "portada_min.jpg");
    {
        Statement up = db_.prepare(
            std::string("UPDATE documento SET portada_propia = 1, portada_foto_id = NULL, actualizado = ") + kNow +
            " WHERE id = ?");
        up.bind(1, id);
        up.step();
    }
    tx.commit();
    return get(id);
}

Json DocumentService::set_cover_photo(std::int64_t id, const Json& payload) {
    if (type_of(id) != "catalogo") throw ValidationFailed({{"portada", "Solo los catálogos tienen portada"}});
    if (!payload.is_object() || !payload.contains("foto_id") ||
        !(payload["foto_id"].is_null() || payload["foto_id"].is_number_integer())) {
        throw ValidationFailed({{"foto_id", "Indique el id de una foto, o null para quitar la portada"}});
    }

    Transaction tx(db_);
    if (payload["foto_id"].is_null()) {
        Statement up = db_.prepare(
            std::string("UPDATE documento SET portada_propia = 0, portada_foto_id = NULL, actualizado = ") + kNow +
            " WHERE id = ?");
        up.bind(1, id);
        up.step();
    } else {
        const std::int64_t photo = payload["foto_id"].get<std::int64_t>();
        {
            Statement chk = db_.prepare(
                "SELECT 1 FROM foto f JOIN documento_casa dc ON dc.casa_id = f.casa_id "
                "WHERE f.id = ? AND dc.documento_id = ?");
            chk.bind(1, photo).bind(2, id);
            if (!chk.step()) throw ValidationFailed({{"foto_id", "La foto no pertenece a una casa del catálogo"}});
        }
        Statement up = db_.prepare(
            std::string("UPDATE documento SET portada_propia = 0, portada_foto_id = ?, actualizado = ") + kNow +
            " WHERE id = ?");
        up.bind(1, photo).bind(2, id);
        up.step();
    }
    tx.commit();
    // la portada subida ya no se usa
    remove_cover_files(id);
    return get(id);
}

fs::path DocumentService::cover_file(std::int64_t id) const {
    Statement st = db_.prepare("SELECT portada_propia FROM documento WHERE id = ?");
    st.bind(1, id);
    if (!st.step() || st.column_int(0) == 0) return {};
    const fs::path p = doc_dir(id) / "portada_opt.jpg";
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : fs::path{};
}

void DocumentService::cleanup_tmp(int max_age_hours) {
    std::error_code ec;
    if (!fs::is_directory(paths_.tmp_dir, ec)) return;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(max_age_hours);
    for (const auto& e : fs::directory_iterator(paths_.tmp_dir, ec)) {
        const std::string name = to_utf8(e.path().filename());
        if (!e.is_directory() || name.rfind("portada-", 0) != 0) continue;
        std::error_code e2;
        if (e.last_write_time(e2) < limit) fs::remove_all(e.path(), e2);
    }
}

}  // namespace wcatalog
