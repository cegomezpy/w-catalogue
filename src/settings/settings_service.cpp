#include "settings/settings_service.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>

namespace wcatalog {

using Json = nlohmann::json;

namespace {

enum class Kind { Color, Font, Currency, Email, Text };

struct Def {
    const char* key;
    const char* default_value;
    Kind kind;
};

const std::vector<Def>& definitions() {
    static const std::vector<Def> defs = {
        {"tema.colorPrimario", "#1F3A5F", Kind::Color},
        {"tema.colorSecundario", "#E07A3F", Kind::Color},
        {"tema.colorTerciario", "#F4EFE6", Kind::Color},
        {"tipografia.fuenteTitulos", "Playfair Display", Kind::Font},
        {"tipografia.fuenteTexto", "Inter", Kind::Font},
        {"contacto.nombre", "", Kind::Text},
        {"contacto.telefono", "", Kind::Text},
        {"contacto.whatsapp", "", Kind::Text},
        {"contacto.email", "", Kind::Email},
        {"contacto.web", "", Kind::Text},
        {"contacto.instagram", "", Kind::Text},
        {"contacto.facebook", "", Kind::Text},
        {"general.moneda", "USD", Kind::Currency},
    };
    return defs;
}

const Def* find_def(const std::string& key) {
    for (const Def& d : definitions()) {
        if (key == d.key) return &d;
    }
    return nullptr;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Longitud en caracteres UTF-8 (no en bytes)
size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

}  // namespace

SettingsService::SettingsService(Database& db) : db_(db) {}

void SettingsService::set_font_validator(std::function<bool(const std::string&)> validator) {
    font_exists_ = std::move(validator);
}

const char* SettingsService::default_value(const std::string& key) {
    const Def* d = find_def(key);
    return d ? d->default_value : "";
}

std::map<std::string, std::string> SettingsService::stored() const {
    std::map<std::string, std::string> out;
    Statement st = db_.prepare("SELECT clave, valor FROM ajustes");
    while (st.step()) out[st.column_text(0)] = st.column_text(1);
    return out;
}

std::string SettingsService::value(const std::string& key) const {
    Statement st = db_.prepare("SELECT valor FROM ajustes WHERE clave = ?");
    st.bind(1, key);
    if (st.step()) return st.column_text(0);
    return default_value(key);
}

Json SettingsService::get() const {
    auto saved = stored();
    Json out = Json::object();
    for (const Def& d : definitions()) {
        const std::string key = d.key;
        const size_t dot = key.find('.');
        auto it = saved.find(key);
        out[key.substr(0, dot)][key.substr(dot + 1)] = it != saved.end() ? it->second : std::string(d.default_value);
    }
    return out;
}

Json SettingsService::update(const Json& patch) {
    std::map<std::string, std::string> fields;       // errores por clave plana
    std::vector<std::pair<std::string, std::string>> writes;

    if (!patch.is_object()) {
        throw ValidationFailed({{"", "Se esperaba un objeto JSON con los ajustes"}});
    }

    static const std::regex hex_re("^#[0-9a-fA-F]{6}$");
    static const std::regex email_re("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$");
    static const std::regex currency_re("^[A-Za-z]{3}$");

    for (const auto& [group, body] : patch.items()) {
        if (!body.is_object()) {
            fields[group] = "Se esperaba un objeto";
            continue;
        }
        for (const auto& [name, raw] : body.items()) {
            const std::string key = group + "." + name;
            const Def* def = find_def(key);
            if (!def) {
                fields[key] = "Ajuste desconocido";
                continue;
            }
            if (!raw.is_string()) {
                fields[key] = "Debe ser un texto";
                continue;
            }
            std::string v = trim(raw.get<std::string>());

            switch (def->kind) {
                case Kind::Color:
                    if (!std::regex_match(v, hex_re)) {
                        fields[key] = "Formato de color inválido; use #RRGGBB (por ejemplo #1F3A5F)";
                        continue;
                    }
                    std::transform(v.begin(), v.end(), v.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    break;
                case Kind::Currency:
                    if (!std::regex_match(v, currency_re)) {
                        fields[key] = "La moneda debe ser un código de 3 letras (por ejemplo USD o EUR)";
                        continue;
                    }
                    std::transform(v.begin(), v.end(), v.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    break;
                case Kind::Email:
                    if (!v.empty() && !std::regex_match(v, email_re)) {
                        fields[key] = "El email no es válido";
                        continue;
                    }
                    break;
                case Kind::Font:
                    if (v.empty() || utf8_length(v) > 100) {
                        fields[key] = "Elija una fuente de la biblioteca";
                        continue;
                    }
                    if (font_exists_ && !font_exists_(v)) {
                        fields[key] = "La fuente \"" + v + "\" no existe en la biblioteca";
                        continue;
                    }
                    break;
                case Kind::Text:
                    if (utf8_length(v) > 200) {
                        fields[key] = "Máximo 200 caracteres";
                        continue;
                    }
                    break;
            }
            writes.emplace_back(key, v);
        }
    }

    if (!fields.empty()) throw ValidationFailed(std::move(fields));

    {
        Transaction tx(db_);
        Statement st = db_.prepare(
            "INSERT INTO ajustes (clave, valor) VALUES (?, ?) "
            "ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor");
        for (const auto& [key, v] : writes) {
            st.bind(1, key).bind(2, v);
            st.step();
            st.reset();
        }
        tx.commit();
    }
    return get();
}

void SettingsService::reset(const std::string& key) {
    Statement st = db_.prepare("DELETE FROM ajustes WHERE clave = ?");
    st.bind(1, key);
    st.step();
}

}  // namespace wcatalog
