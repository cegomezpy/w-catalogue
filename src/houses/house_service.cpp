#include "houses/house_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>

#include "domain/money.hpp"
#include "domain/text_fold.hpp"
#include "houses/badges.hpp"
#include "photos/photo_service.hpp"

namespace wcatalog {

namespace {

Json cover_for(const std::map<std::int64_t, std::string>& covers, std::int64_t id) {
    auto it = covers.find(id);
    return it == covers.end() ? Json(nullptr) : Json(it->second);
}

const char* const kColumns =
    "id, titulo, descripcion, operacion, estado, precio_centavos, moneda, sin_precio, cuartos, banos, "
    "comedores_independientes, niveles, area_construida_m2, area_terreno_m2, direccion, barrio, municipio, "
    "provincia, lat, lon, creado, actualizado";

const char* const kNow = "strftime('%Y-%m-%dT%H:%M:%fZ', 'now')";

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool one_of(const std::string& v, std::initializer_list<const char*> allowed) {
    for (const char* a : allowed) {
        if (v == a) return true;
    }
    return false;
}

// null, "" o ausente -> sin valor
bool is_blank(const Json& j) { return j.is_null() || (j.is_string() && trim(j.get<std::string>()).empty()); }

std::optional<double> number_of(const Json& j, bool& ok) {
    ok = true;
    if (j.is_number()) return j.get<double>();
    if (j.is_string()) {
        const std::string s = trim(j.get<std::string>());
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') return d;
    }
    ok = false;
    return std::nullopt;
}

}  // namespace

struct HouseService::Parsed {
    std::string title, description, state = "disponible", no_price = "consultar";
    std::optional<std::string> operation, currency;
    std::optional<std::int64_t> price_cents;
    std::optional<std::int64_t> rooms, baths, dining, levels;
    std::optional<double> built_m2, land_m2;
    std::string address, neighborhood, municipality, province;
    std::optional<double> lat, lon;
    std::map<std::int64_t, std::string> values;  // id de característica -> valor serializado
};

HouseService::HouseService(Database& db, SettingsService& settings, FeatureService& features)
    : db_(db), settings_(settings), features_(features) {}

HouseService::Parsed HouseService::parse(const Json& payload) const {
    std::map<std::string, std::string> errors;
    Parsed p;

    if (!payload.is_object()) throw ValidationFailed({{"", "Se esperaba un objeto JSON"}});

    auto text = [&](const char* key, std::string& target, size_t max_chars) {
        if (!payload.contains(key) || payload[key].is_null()) return;
        if (!payload[key].is_string()) {
            errors[key] = "Debe ser un texto";
            return;
        }
        target = trim(payload[key].get<std::string>());
        if (utf8_length(target) > max_chars) errors[key] = "Máximo " + std::to_string(max_chars) + " caracteres";
    };

    // título
    if (!payload.contains("titulo") || !payload["titulo"].is_string() || trim(payload["titulo"].get<std::string>()).empty()) {
        errors["titulo"] = "El título es obligatorio";
    } else {
        p.title = trim(payload["titulo"].get<std::string>());
        if (utf8_length(p.title) > 200) errors["titulo"] = "Máximo 200 caracteres";
    }
    text("descripcion", p.description, 5000);
    text("direccion", p.address, 200);
    text("barrio", p.neighborhood, 200);
    text("municipio", p.municipality, 200);
    text("provincia", p.province, 200);

    // operación y estado
    if (payload.contains("operacion") && !is_blank(payload["operacion"])) {
        if (!payload["operacion"].is_string() || !one_of(payload["operacion"].get<std::string>(), {"venta", "alquiler"})) {
            errors["operacion"] = "La operación debe ser venta o alquiler";
        } else {
            p.operation = payload["operacion"].get<std::string>();
        }
    }
    if (payload.contains("estado") && !payload["estado"].is_null()) {
        if (!payload["estado"].is_string() ||
            !one_of(payload["estado"].get<std::string>(), {"disponible", "reservada", "vendida", "alquilada"})) {
            errors["estado"] = "El estado debe ser disponible, reservada, vendida o alquilada";
        } else {
            p.state = payload["estado"].get<std::string>();
        }
    }

    // precio y moneda
    if (payload.contains("sin_precio") && !payload["sin_precio"].is_null()) {
        if (!payload["sin_precio"].is_string() || !one_of(payload["sin_precio"].get<std::string>(), {"consultar", "ocultar"})) {
            errors["sin_precio"] = "Debe ser consultar u ocultar";
        } else {
            p.no_price = payload["sin_precio"].get<std::string>();
        }
    }
    if (payload.contains("precio") && !is_blank(payload["precio"])) {
        bool ok = false;
        auto d = number_of(payload["precio"], ok);
        if (!ok) {
            errors["precio"] = "El precio debe ser un número";
        } else {
            try {
                p.price_cents = price_to_cents(*d);
            } catch (const std::invalid_argument& e) {
                errors["precio"] = e.what();
            }
        }
    }
    if (p.price_cents) {
        std::string cur;
        if (payload.contains("moneda") && !is_blank(payload["moneda"])) {
            if (!payload["moneda"].is_string()) errors["moneda"] = "Debe ser un código de 3 letras";
            else cur = upper(trim(payload["moneda"].get<std::string>()));
        } else {
            cur = settings_.value("general.moneda");
        }
        const bool letters = cur.size() == 3 && std::all_of(cur.begin(), cur.end(), [](unsigned char c) { return std::isalpha(c); });
        if (!errors.count("moneda")) {
            if (!letters) errors["moneda"] = "La moneda debe ser un código de 3 letras (por ejemplo USD)";
            else p.currency = cur;
        }
    }

    // enteros no negativos
    auto integer = [&](const char* key, std::optional<std::int64_t>& target) {
        if (!payload.contains(key) || is_blank(payload[key])) return;
        bool ok = false;
        auto d = number_of(payload[key], ok);
        if (!ok || !std::isfinite(*d) || *d != std::floor(*d)) {
            errors[key] = "Debe ser un número entero";
        } else if (*d < 0) {
            errors[key] = "No puede ser negativo";
        } else if (*d > 100000) {
            errors[key] = "El número es demasiado grande";
        } else {
            target = static_cast<std::int64_t>(*d);
        }
    };
    integer("cuartos", p.rooms);
    integer("banos", p.baths);
    integer("comedores_independientes", p.dining);
    integer("niveles", p.levels);

    auto area = [&](const char* key, std::optional<double>& target) {
        if (!payload.contains(key) || is_blank(payload[key])) return;
        bool ok = false;
        auto d = number_of(payload[key], ok);
        if (!ok || !std::isfinite(*d)) errors[key] = "Debe ser un número";
        else if (*d < 0) errors[key] = "No puede ser negativo";
        else if (*d > 1e7) errors[key] = "El área es demasiado grande";
        else target = std::round(*d * 100.0) / 100.0;
    };
    area("area_construida_m2", p.built_m2);
    area("area_terreno_m2", p.land_m2);

    // coordenadas: ambas o ninguna, dentro de rango
    {
        const bool has_lat = payload.contains("lat") && !is_blank(payload["lat"]);
        const bool has_lon = payload.contains("lon") && !is_blank(payload["lon"]);
        if (has_lat != has_lon) {
            errors["coordenadas"] = "Indique latitud y longitud, o deje ambas vacías";
        } else if (has_lat) {
            bool ok1 = false, ok2 = false;
            auto la = number_of(payload["lat"], ok1);
            auto lo = number_of(payload["lon"], ok2);
            if (!ok1 || !ok2 || !std::isfinite(*la) || !std::isfinite(*lo)) {
                errors["coordenadas"] = "Las coordenadas deben ser números";
            } else if (*la < -90 || *la > 90) {
                errors["coordenadas"] = "La latitud debe estar entre -90 y 90";
            } else if (*lo < -180 || *lo > 180) {
                errors["coordenadas"] = "La longitud debe estar entre -180 y 180";
            } else {
                p.lat = *la;
                p.lon = *lo;
            }
        }
    }

    // valores de características
    if (payload.contains("caracteristicas") && !payload["caracteristicas"].is_null()) {
        if (!payload["caracteristicas"].is_array()) {
            errors["caracteristicas"] = "Debe ser una lista de {caracteristica_id, valor}";
        } else {
            std::map<std::int64_t, Feature> catalog;
            for (Feature& f : features_.list()) catalog.emplace(f.id, f);
            for (const Json& item : payload["caracteristicas"]) {
                if (!item.is_object() || !item.contains("caracteristica_id") || !item["caracteristica_id"].is_number_integer()) {
                    errors["caracteristicas"] = "Cada elemento necesita un caracteristica_id entero";
                    continue;
                }
                const std::int64_t id = item["caracteristica_id"].get<std::int64_t>();
                const std::string key = "caracteristicas." + std::to_string(id);
                auto it = catalog.find(id);
                if (it == catalog.end()) {
                    errors[key] = "La característica no existe";
                    continue;
                }
                if (p.values.count(id) || errors.count(key)) {
                    errors[key] = "La característica está repetida";
                    continue;
                }
                try {
                    auto v = serialize_feature_value(it->second, item.contains("valor") ? item["valor"] : Json(nullptr));
                    if (v) p.values[id] = *v;
                } catch (const ValueError& e) {
                    errors[key] = e.what();
                }
            }
        }
    }

    if (!errors.empty()) throw ValidationFailed(std::move(errors));
    return p;
}

void HouseService::write_values(std::int64_t house_id, const Parsed& p) {
    // Se reemplazan los valores de las características activas. Los de las inactivas se conservan
    // (solo se pisan si vienen en el payload), porque el formulario no las muestra.
    {
        Statement del = db_.prepare(
            "DELETE FROM casa_caracteristica WHERE casa_id = ? AND caracteristica_id IN "
            "(SELECT id FROM caracteristica WHERE activa = 1)");
        del.bind(1, house_id);
        del.step();
    }
    Statement ins = db_.prepare(
        "INSERT OR REPLACE INTO casa_caracteristica (casa_id, caracteristica_id, valor) VALUES (?, ?, ?)");
    for (const auto& [fid, value] : p.values) {
        ins.bind(1, house_id).bind(2, fid).bind(3, value);
        ins.step();
        ins.reset();
    }
}

namespace {

void bind_opt_text(Statement& st, int i, const std::optional<std::string>& v) {
    if (v) st.bind(i, *v);
    else st.bind_null(i);
}
void bind_opt_int(Statement& st, int i, const std::optional<std::int64_t>& v) {
    if (v) st.bind(i, *v);
    else st.bind_null(i);
}
void bind_opt_real(Statement& st, int i, const std::optional<double>& v) {
    if (v) st.bind(i, *v);
    else st.bind_null(i);
}

}  // namespace

Json HouseService::create(const Json& payload) {
    Parsed p = parse(payload);
    Transaction tx(db_);
    Statement ins = db_.prepare(
        std::string("INSERT INTO casa (titulo, descripcion, operacion, estado, precio_centavos, moneda, sin_precio, cuartos, "
                    "banos, comedores_independientes, niveles, area_construida_m2, area_terreno_m2, direccion, barrio, "
                    "municipio, provincia, lat, lon, texto_busqueda, creado, actualizado) VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ") + kNow + ", " + kNow + ")");
    ins.bind(1, p.title).bind(2, p.description);
    bind_opt_text(ins, 3, p.operation);
    ins.bind(4, p.state);
    bind_opt_int(ins, 5, p.price_cents);
    bind_opt_text(ins, 6, p.currency);
    ins.bind(7, p.no_price);
    bind_opt_int(ins, 8, p.rooms);
    bind_opt_int(ins, 9, p.baths);
    bind_opt_int(ins, 10, p.dining);
    bind_opt_int(ins, 11, p.levels);
    bind_opt_real(ins, 12, p.built_m2);
    bind_opt_real(ins, 13, p.land_m2);
    ins.bind(14, p.address).bind(15, p.neighborhood).bind(16, p.municipality).bind(17, p.province);
    bind_opt_real(ins, 18, p.lat);
    bind_opt_real(ins, 19, p.lon);
    ins.bind(20, fold_text(p.title + " " + p.neighborhood + " " + p.municipality));
    ins.step();
    const std::int64_t id = db_.last_insert_rowid();
    write_values(id, p);
    tx.commit();
    return get(id);
}

Json HouseService::update(std::int64_t id, const Json& payload) {
    {
        Statement st = db_.prepare("SELECT 1 FROM casa WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw NotFoundError("La casa no existe");
    }
    Parsed p = parse(payload);
    Transaction tx(db_);
    Statement up = db_.prepare(
        std::string("UPDATE casa SET titulo = ?, descripcion = ?, operacion = ?, estado = ?, precio_centavos = ?, moneda = ?, "
                    "sin_precio = ?, cuartos = ?, banos = ?, comedores_independientes = ?, niveles = ?, "
                    "area_construida_m2 = ?, area_terreno_m2 = ?, direccion = ?, barrio = ?, municipio = ?, provincia = ?, "
                    "lat = ?, lon = ?, texto_busqueda = ?, actualizado = ") + kNow + " WHERE id = ?");
    up.bind(1, p.title).bind(2, p.description);
    bind_opt_text(up, 3, p.operation);
    up.bind(4, p.state);
    bind_opt_int(up, 5, p.price_cents);
    bind_opt_text(up, 6, p.currency);
    up.bind(7, p.no_price);
    bind_opt_int(up, 8, p.rooms);
    bind_opt_int(up, 9, p.baths);
    bind_opt_int(up, 10, p.dining);
    bind_opt_int(up, 11, p.levels);
    bind_opt_real(up, 12, p.built_m2);
    bind_opt_real(up, 13, p.land_m2);
    up.bind(14, p.address).bind(15, p.neighborhood).bind(16, p.municipality).bind(17, p.province);
    bind_opt_real(up, 18, p.lat);
    bind_opt_real(up, 19, p.lon);
    up.bind(20, fold_text(p.title + " " + p.neighborhood + " " + p.municipality));
    up.bind(21, id);
    up.step();
    write_values(id, p);
    tx.commit();
    return get(id);
}

void HouseService::remove(std::int64_t id) {
    Statement st = db_.prepare("DELETE FROM casa WHERE id = ?");
    st.bind(1, id);
    st.step();
    if (db_.changes() == 0) throw NotFoundError("La casa no existe");
    if (photos_) photos_->remove_house_folder(id);
}

Json HouseService::row_to_json(Statement& st) const {
    auto opt_int = [&](int c) { return st.is_null(c) ? Json(nullptr) : Json(st.column_int(c)); };
    auto opt_real = [&](int c) { return st.is_null(c) ? Json(nullptr) : Json(st.column_double(c)); };
    auto opt_text = [&](int c) { return st.is_null(c) ? Json(nullptr) : Json(st.column_text(c)); };
    return Json{{"id", st.column_int(0)},
                {"titulo", st.column_text(1)},
                {"descripcion", st.column_text(2)},
                {"operacion", opt_text(3)},
                {"estado", st.column_text(4)},
                {"precio", st.is_null(5) ? Json(nullptr) : Json(cents_to_price(st.column_int(5)))},
                {"moneda", opt_text(6)},
                {"sin_precio", st.column_text(7)},
                {"cuartos", opt_int(8)},
                {"banos", opt_int(9)},
                {"comedores_independientes", opt_int(10)},
                {"niveles", opt_int(11)},
                {"area_construida_m2", opt_real(12)},
                {"area_terreno_m2", opt_real(13)},
                {"direccion", st.column_text(14)},
                {"barrio", st.column_text(15)},
                {"municipio", st.column_text(16)},
                {"provincia", st.column_text(17)},
                {"lat", opt_real(18)},
                {"lon", opt_real(19)},
                {"creado", st.column_text(20)},
                {"actualizado", st.column_text(21)}};
}

Json HouseService::get(std::int64_t id) const {
    Json house;
    {
        Statement st = db_.prepare(std::string("SELECT ") + kColumns + " FROM casa WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw NotFoundError("La casa no existe");
        house = row_to_json(st);
    }

    std::vector<Feature> catalog = features_.list();
    std::map<std::int64_t, const Feature*> by_id;
    for (const Feature& f : catalog) by_id[f.id] = &f;

    std::map<std::int64_t, std::string> values;
    Json list = Json::array();
    {
        Statement st = db_.prepare(
            "SELECT cc.caracteristica_id, cc.valor FROM casa_caracteristica cc JOIN caracteristica c "
            "ON c.id = cc.caracteristica_id WHERE cc.casa_id = ? ORDER BY c.orden, c.id");
        st.bind(1, id);
        while (st.step()) {
            const std::int64_t fid = st.column_int(0);
            const std::string stored = st.column_text(1);
            values[fid] = stored;
            auto it = by_id.find(fid);
            if (it != by_id.end()) {
                list.push_back(Json{{"caracteristica_id", fid}, {"valor", deserialize_feature_value(*it->second, stored)}});
            }
        }
    }
    house["caracteristicas"] = list;
    house["badges"] = badges_to_json(resolve_badges(catalog, values));
    house["fotos"] = photos_ ? photos_->list(id) : Json::array();
    return house;
}

Json HouseService::list(const std::string& q, const std::string& estado) const {
    std::string sql = std::string("SELECT ") + kColumns + " FROM casa WHERE 1 = 1";
    std::vector<std::string> binds;

    if (!estado.empty()) {
        if (!one_of(estado, {"disponible", "reservada", "vendida", "alquilada"})) {
            throw ValidationFailed({{"estado", "El estado debe ser disponible, reservada, vendida o alquilada"}});
        }
        sql += " AND estado = ?";
        binds.push_back(estado);
    }

    // cada palabra de la búsqueda debe aparecer (sin tildes ni mayúsculas) en título, barrio o municipio
    std::istringstream words(fold_text(q));
    std::string word;
    while (words >> word) {
        std::string escaped;
        for (char c : word) {
            if (c == '%' || c == '_' || c == '\\') escaped += '\\';
            escaped += c;
        }
        sql += " AND texto_busqueda LIKE ? ESCAPE '\\'";
        binds.push_back("%" + escaped + "%");
    }
    sql += " ORDER BY actualizado DESC, id DESC";

    const std::map<std::int64_t, std::string> covers =
        photos_ ? photos_->cover_thumbnails() : std::map<std::int64_t, std::string>{};

    Statement st = db_.prepare(sql);
    for (size_t i = 0; i < binds.size(); ++i) st.bind(static_cast<int>(i) + 1, binds[i]);

    Json out = Json::array();
    while (st.step()) {
        Json full = row_to_json(st);
        out.push_back(Json{{"id", full["id"]},
                           {"titulo", full["titulo"]},
                           {"estado", full["estado"]},
                           {"operacion", full["operacion"]},
                           {"precio", full["precio"]},
                           {"moneda", full["moneda"]},
                           {"sin_precio", full["sin_precio"]},
                           {"barrio", full["barrio"]},
                           {"municipio", full["municipio"]},
                           {"cuartos", full["cuartos"]},
                           {"banos", full["banos"]},
                           {"actualizado", full["actualizado"]},
                           {"portada_min", cover_for(covers, full["id"].get<std::int64_t>())}});
    }
    return out;
}

Json HouseService::preview_badges(const Json& feature_values) const {
    Json payload = {{"titulo", "vista previa"}, {"caracteristicas", feature_values}};
    Parsed p = parse(payload);
    return badges_to_json(resolve_badges(features_.list(), p.values));
}

}  // namespace wcatalog
