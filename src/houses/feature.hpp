#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wcatalog {

using Json = nlohmann::json;

struct Feature {
    std::int64_t id = 0;
    std::string key;
    std::string label;
    std::string category;  // servicios | espacios | condicion | otra
    std::string type;      // bool | numero | texto | opcion | multiopcion
    std::vector<std::string> options;
    std::string unit;
    std::string icon;
    bool badge_visible = true;
    std::optional<std::string> badge_text;  // plantilla con {valor}; sin valor = texto por defecto
    std::string badge_role = "secundario";  // primario | secundario | terciario
    int order = 0;
    bool active = true;
    bool predefined = false;
};

// El valor no cumple el tipo de la característica; el mensaje es apto para mostrar al usuario.
struct ValueError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

bool is_option_type(const std::string& type);

// Valida el valor recibido por la API y lo serializa para guardarlo:
//   bool -> "1"/"0"; numero -> decimal en texto; texto -> texto; opcion -> la opción;
//   multiopcion -> arreglo JSON.
// Devuelve nullopt si el valor equivale a "no informado" (null, texto vacío, arreglo vacío).
// Lanza ValueError si no es válido (número negativo, opción fuera de la lista, tipo incorrecto...).
std::optional<std::string> serialize_feature_value(const Feature& f, const Json& value);

// Inverso de serialize_feature_value, para la API.
Json deserialize_feature_value(const Feature& f, const std::string& stored);

// Valor legible para los badges: opción múltiple unida con ", ", número con su unidad, sí/no como "Sí"/"No".
std::string display_feature_value(const Feature& f, const std::string& stored);

}  // namespace wcatalog
