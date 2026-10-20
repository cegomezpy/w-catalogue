#include "houses/feature_service.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "domain/text_fold.hpp"

namespace wcatalog {

namespace {

const char* const kSelect =
    "SELECT id, clave, etiqueta, categoria, tipo, opciones, unidad, icono, badge_visible, badge_texto, "
    "badge_rol, orden, activa, predefinida FROM caracteristica";

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

bool one_of(const std::string& v, std::initializer_list<const char*> allowed) {
    for (const char* a : allowed) {
        if (v == a) return true;
    }
    return false;
}

bool icon_name_ok(const std::string& s) {
    if (s.size() > 40) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '-'; });
}

}  // namespace

FeatureService::FeatureService(Database& db) : db_(db) {}

Feature FeatureService::read_row(Statement& st) {
    Feature f;
    f.id = st.column_int(0);
    f.key = st.column_text(1);
    f.label = st.column_text(2);
    f.category = st.column_text(3);
    f.type = st.column_text(4);
    Json opts = Json::parse(st.column_text(5), nullptr, false);
    if (opts.is_array()) {
        for (const Json& o : opts) {
            if (o.is_string()) f.options.push_back(o.get<std::string>());
        }
    }
    f.unit = st.column_text(6);
    f.icon = st.column_text(7);
    f.badge_visible = st.column_int(8) != 0;
    if (!st.is_null(9)) f.badge_text = st.column_text(9);
    f.badge_role = st.column_text(10);
    f.order = static_cast<int>(st.column_int(11));
    f.active = st.column_int(12) != 0;
    f.predefined = st.column_int(13) != 0;
    return f;
}

std::vector<Feature> FeatureService::list(bool only_active) const {
    std::string sql = kSelect;
    if (only_active) sql += " WHERE activa = 1";
    sql += " ORDER BY orden, id";
    Statement st = db_.prepare(sql);
    std::vector<Feature> out;
    while (st.step()) out.push_back(read_row(st));
    return out;
}

Feature FeatureService::get(std::int64_t id) const {
    Statement st = db_.prepare(std::string(kSelect) + " WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) throw NotFoundError("La característica no existe");
    return read_row(st);
}

int FeatureService::usage_count(std::int64_t id) const {
    Statement st = db_.prepare("SELECT COUNT(*) FROM casa_caracteristica WHERE caracteristica_id = ?");
    st.bind(1, id);
    st.step();
    return static_cast<int>(st.column_int(0));
}

int FeatureService::option_usage(const Feature& f, const std::string& option) const {
    const char* sql = f.type == "multiopcion"
                          ? "SELECT COUNT(*) FROM casa_caracteristica WHERE caracteristica_id = ? AND EXISTS "
                            "(SELECT 1 FROM json_each(valor) WHERE value = ?)"
                          : "SELECT COUNT(*) FROM casa_caracteristica WHERE caracteristica_id = ? AND valor = ?";
    Statement st = db_.prepare(sql);
    st.bind(1, f.id).bind(2, option);
    st.step();
    return static_cast<int>(st.column_int(0));
}

Json FeatureService::to_json(const Feature& f, int usage) const {
    Json j = {{"id", f.id},
              {"clave", f.key},
              {"etiqueta", f.label},
              {"categoria", f.category},
              {"tipo", f.type},
              {"opciones", f.options},
              {"unidad", f.unit},
              {"icono", f.icon},
              {"badge", {{"visible", f.badge_visible},
                         {"texto", f.badge_text ? Json(*f.badge_text) : Json(nullptr)},
                         {"rol", f.badge_role}}},
              {"orden", f.order},
              {"activa", f.active},
              {"predefinida", f.predefined}};
    j["casas_con_valor"] = usage >= 0 ? usage : usage_count(f.id);
    return j;
}

Feature FeatureService::parse(const Feature& base, const Json& payload, bool is_create) const {
    std::map<std::string, std::string> errors;
    Feature f = base;

    if (!payload.is_object()) throw ValidationFailed({{"", "Se esperaba un objeto JSON"}});

    auto get_string = [&](const char* key, std::string& target, bool required) {
        if (!payload.contains(key)) {
            if (required && is_create) errors[key] = "Es obligatorio";
            return false;
        }
        if (!payload[key].is_string()) {
            errors[key] = "Debe ser un texto";
            return false;
        }
        target = trim(payload[key].get<std::string>());
        return true;
    };

    if (get_string("etiqueta", f.label, true)) {
        if (f.label.empty()) errors["etiqueta"] = "La etiqueta es obligatoria";
        else if (utf8_length(f.label) > 80) errors["etiqueta"] = "Máximo 80 caracteres";
    }
    if (get_string("categoria", f.category, true) &&
        !one_of(f.category, {"servicios", "espacios", "condicion", "otra"})) {
        errors["categoria"] = "La categoría debe ser servicios, espacios, condicion u otra";
    }
    if (get_string("tipo", f.type, true) && !one_of(f.type, {"bool", "numero", "texto", "opcion", "multiopcion"})) {
        errors["tipo"] = "El tipo debe ser bool, numero, texto, opcion o multiopcion";
    }
    if (get_string("unidad", f.unit, false) && utf8_length(f.unit) > 20) errors["unidad"] = "Máximo 20 caracteres";
    if (get_string("icono", f.icon, false) && !icon_name_ok(f.icon)) errors["icono"] = "Nombre de icono no válido";

    if (payload.contains("opciones")) {
        if (!payload["opciones"].is_array()) {
            errors["opciones"] = "Debe ser una lista de textos";
        } else {
            f.options.clear();
            std::set<std::string> seen;
            for (const Json& o : payload["opciones"]) {
                if (!o.is_string()) {
                    errors["opciones"] = "Cada opción debe ser un texto";
                    break;
                }
                const std::string s = trim(o.get<std::string>());
                if (s.empty()) continue;
                if (utf8_length(s) > 60) {
                    errors["opciones"] = "Cada opción admite hasta 60 caracteres";
                    break;
                }
                if (seen.insert(s).second) f.options.push_back(s);
            }
        }
    }

    if (payload.contains("activa")) {
        if (!payload["activa"].is_boolean()) errors["activa"] = "Debe ser verdadero o falso";
        else f.active = payload["activa"].get<bool>();
    }

    if (payload.contains("badge")) {
        const Json& b = payload["badge"];
        if (!b.is_object()) {
            errors["badge"] = "Debe ser un objeto";
        } else {
            if (b.contains("visible")) {
                if (!b["visible"].is_boolean()) errors["badge.visible"] = "Debe ser verdadero o falso";
                else f.badge_visible = b["visible"].get<bool>();
            }
            if (b.contains("texto")) {
                if (b["texto"].is_null()) {
                    f.badge_text.reset();
                } else if (!b["texto"].is_string()) {
                    errors["badge.texto"] = "Debe ser un texto";
                } else {
                    std::string t = trim(b["texto"].get<std::string>());
                    if (utf8_length(t) > 100) errors["badge.texto"] = "Máximo 100 caracteres";
                    else if (t.empty()) f.badge_text.reset();
                    else f.badge_text = t;
                }
            }
            if (b.contains("rol")) {
                if (!b["rol"].is_string() || !one_of(b["rol"].get<std::string>(), {"primario", "secundario", "terciario"})) {
                    errors["badge.rol"] = "El rol debe ser primario, secundario o terciario";
                } else {
                    f.badge_role = b["rol"].get<std::string>();
                }
            }
        }
    }

    // los tipos sin lista de opciones no la guardan; los de opción la exigen
    if (!errors.count("tipo") && !errors.count("opciones")) {
        if (is_option_type(f.type)) {
            if (f.options.empty()) errors["opciones"] = "Defina al menos una opción";
        } else {
            f.options.clear();
        }
    }

    if (!errors.empty()) throw ValidationFailed(std::move(errors));
    return f;
}

std::string FeatureService::unique_key(const std::string& label) const {
    std::string base;
    for (unsigned char c : fold_text(label)) {
        if (std::isalnum(c) && c < 0x80) base += static_cast<char>(c);
        else if (!base.empty() && base.back() != '_') base += '_';
    }
    while (!base.empty() && base.back() == '_') base.pop_back();
    if (base.empty()) base = "caracteristica";
    if (base.size() > 40) base.resize(40);

    std::string key = base;
    for (int n = 2;; ++n) {
        Statement st = db_.prepare("SELECT COUNT(*) FROM caracteristica WHERE clave = ?");
        st.bind(1, key);
        st.step();
        if (st.column_int(0) == 0) return key;
        key = base + "_" + std::to_string(n);
    }
}

Feature FeatureService::create(const Json& payload) {
    Feature blank;
    blank.badge_role = "secundario";
    Feature f = parse(blank, payload, true);

    Transaction tx(db_);
    f.key = unique_key(f.label);
    {
        Statement st = db_.prepare("SELECT COALESCE(MAX(orden), 0) + 1 FROM caracteristica");
        st.step();
        f.order = static_cast<int>(st.column_int(0));
    }
    Statement ins = db_.prepare(
        "INSERT INTO caracteristica (clave, etiqueta, categoria, tipo, opciones, unidad, icono, badge_visible, "
        "badge_texto, badge_rol, orden, activa, predefinida) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
    ins.bind(1, f.key).bind(2, f.label).bind(3, f.category).bind(4, f.type);
    ins.bind(5, Json(f.options).dump(-1, ' ', false, Json::error_handler_t::replace));
    ins.bind(6, f.unit).bind(7, f.icon).bind(8, std::int64_t{f.badge_visible ? 1 : 0});
    if (f.badge_text) ins.bind(9, *f.badge_text);
    else ins.bind_null(9);
    ins.bind(10, f.badge_role).bind(11, std::int64_t{f.order}).bind(12, std::int64_t{f.active ? 1 : 0});
    ins.step();
    f.id = db_.last_insert_rowid();
    tx.commit();
    return get(f.id);
}

Feature FeatureService::update(std::int64_t id, const Json& payload) {
    Feature old = get(id);
    Feature f = parse(old, payload, false);

    if (f.type != old.type) {
        const int used = usage_count(id);
        if (used > 0) {
            throw ConflictError("No se puede cambiar el tipo: " + std::to_string(used) +
                                (used == 1 ? " casa ya tiene" : " casas ya tienen") + " un valor para \"" + old.label + "\"");
        }
    } else if (is_option_type(old.type)) {
        for (const std::string& opt : old.options) {
            if (std::find(f.options.begin(), f.options.end(), opt) != f.options.end()) continue;
            const int used = option_usage(old, opt);
            if (used > 0) {
                throw ConflictError("No se puede quitar la opción \"" + opt + "\": la usa" + (used == 1 ? " 1 casa" : "n " + std::to_string(used) + " casas") +
                                    ". Cambie esas casas primero.");
            }
        }
    }

    Transaction tx(db_);
    Statement up = db_.prepare(
        "UPDATE caracteristica SET etiqueta = ?, categoria = ?, tipo = ?, opciones = ?, unidad = ?, icono = ?, "
        "badge_visible = ?, badge_texto = ?, badge_rol = ?, activa = ? WHERE id = ?");
    up.bind(1, f.label).bind(2, f.category).bind(3, f.type);
    up.bind(4, Json(f.options).dump(-1, ' ', false, Json::error_handler_t::replace));
    up.bind(5, f.unit).bind(6, f.icon).bind(7, std::int64_t{f.badge_visible ? 1 : 0});
    if (f.badge_text) up.bind(8, *f.badge_text);
    else up.bind_null(8);
    up.bind(9, f.badge_role).bind(10, std::int64_t{f.active ? 1 : 0}).bind(11, id);
    up.step();
    tx.commit();
    return get(id);
}

void FeatureService::remove(std::int64_t id) {
    Feature f = get(id);
    if (f.predefined) {
        throw ConflictError("\"" + f.label + "\" es una característica predefinida y no se puede eliminar; puede desactivarla");
    }
    Statement st = db_.prepare("DELETE FROM caracteristica WHERE id = ?");
    st.bind(1, id);
    st.step();  // los valores se borran en cascada
}

void FeatureService::reorder(const std::vector<std::int64_t>& ids) {
    std::set<std::int64_t> given;
    for (std::int64_t id : ids) {
        if (!given.insert(id).second) throw ValidationFailed({{"ids", "Hay identificadores repetidos"}});
    }
    std::vector<Feature> all = list();
    std::set<std::int64_t> known;
    for (const Feature& f : all) known.insert(f.id);
    for (std::int64_t id : ids) {
        if (!known.count(id)) throw ValidationFailed({{"ids", "La característica " + std::to_string(id) + " no existe"}});
    }

    std::vector<std::int64_t> final_order = ids;
    for (const Feature& f : all) {
        if (!given.count(f.id)) final_order.push_back(f.id);
    }
    Transaction tx(db_);
    Statement st = db_.prepare("UPDATE caracteristica SET orden = ? WHERE id = ?");
    int n = 1;
    for (std::int64_t id : final_order) {
        st.bind(1, std::int64_t{n++}).bind(2, id);
        st.step();
        st.reset();
    }
    tx.commit();
}

}  // namespace wcatalog
