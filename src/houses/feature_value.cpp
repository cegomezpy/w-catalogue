#include "houses/feature.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace wcatalog {

namespace {

constexpr size_t kMaxTextChars = 500;
constexpr double kMaxNumber = 1e9;

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

bool contains_option(const Feature& f, const std::string& v) {
    return std::find(f.options.begin(), f.options.end(), v) != f.options.end();
}

std::string format_number(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", d);
    return buf;
}

}  // namespace

bool is_option_type(const std::string& type) { return type == "opcion" || type == "multiopcion"; }

std::optional<std::string> serialize_feature_value(const Feature& f, const Json& value) {
    if (value.is_null()) return std::nullopt;

    if (f.type == "bool") {
        if (value.is_boolean()) return std::string(value.get<bool>() ? "1" : "0");
        if (value.is_string() && (value == "1" || value == "0")) return value.get<std::string>();
        throw ValueError("Debe ser sí o no");
    }

    if (f.type == "numero") {
        double d = 0;
        if (value.is_number()) {
            d = value.get<double>();
        } else if (value.is_string()) {
            const std::string s = trim(value.get<std::string>());
            if (s.empty()) return std::nullopt;
            char* end = nullptr;
            d = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0') throw ValueError("Debe ser un número");
        } else {
            throw ValueError("Debe ser un número");
        }
        if (!std::isfinite(d)) throw ValueError("Debe ser un número");
        if (d < 0) throw ValueError("No puede ser negativo");
        if (d > kMaxNumber) throw ValueError("El número es demasiado grande");
        return format_number(d);
    }

    if (f.type == "texto") {
        if (!value.is_string()) throw ValueError("Debe ser un texto");
        const std::string s = trim(value.get<std::string>());
        if (s.empty()) return std::nullopt;
        if (utf8_length(s) > kMaxTextChars) throw ValueError("Máximo 500 caracteres");
        return s;
    }

    if (f.type == "opcion") {
        if (!value.is_string()) throw ValueError("Elija una de las opciones");
        const std::string s = value.get<std::string>();
        if (s.empty()) return std::nullopt;
        if (!contains_option(f, s)) throw ValueError("\"" + s + "\" no está entre las opciones de " + f.label);
        return s;
    }

    if (f.type == "multiopcion") {
        if (!value.is_array()) throw ValueError("Elija una o más de las opciones");
        std::set<std::string> seen;
        Json out = Json::array();
        for (const Json& item : value) {
            if (!item.is_string()) throw ValueError("Elija una o más de las opciones");
            const std::string s = item.get<std::string>();
            if (!contains_option(f, s)) throw ValueError("\"" + s + "\" no está entre las opciones de " + f.label);
            if (seen.insert(s).second) out.push_back(s);
        }
        if (out.empty()) return std::nullopt;
        return out.dump(-1, ' ', false, Json::error_handler_t::replace);
    }

    throw ValueError("Tipo de característica desconocido");
}

Json deserialize_feature_value(const Feature& f, const std::string& stored) {
    if (f.type == "bool") return stored == "1";
    if (f.type == "numero") return std::strtod(stored.c_str(), nullptr);
    if (f.type == "multiopcion") {
        Json arr = Json::parse(stored, nullptr, false);
        return arr.is_array() ? arr : Json::array();
    }
    return stored;
}

std::string display_feature_value(const Feature& f, const std::string& stored) {
    if (f.type == "bool") return stored == "1" ? "Sí" : "No";
    if (f.type == "numero") {
        std::string s = format_number(std::strtod(stored.c_str(), nullptr));
        if (!f.unit.empty()) s += " " + f.unit;
        return s;
    }
    if (f.type == "multiopcion") {
        Json arr = Json::parse(stored, nullptr, false);
        std::string out;
        if (arr.is_array()) {
            for (const Json& item : arr) {
                if (!item.is_string()) continue;
                if (!out.empty()) out += ", ";
                out += item.get<std::string>();
            }
        }
        return out;
    }
    return stored;
}

}  // namespace wcatalog
