#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "houses/feature.hpp"

namespace wcatalog {

struct Badge {
    std::int64_t feature_id = 0;
    std::string text;
    std::string role;  // primario | secundario | terciario
    std::string icon;
};

// Badges que corresponden a una casa (función pura, usada por la API y por la exportación):
//  1. solo características activas, con badge visible y con valor; en las sí/no, solo las que valen "1";
//  2. en el orden del catálogo;
//  3. texto = plantilla con {valor} sustituido, o el texto por defecto (la etiqueta en sí/no,
//     "etiqueta: valor" en el resto);
//  4. opción múltiple unida con ", " y unidad añadida a los números.
// `values` mapea el id de la característica con el valor guardado (serializado).
std::vector<Badge> resolve_badges(const std::vector<Feature>& catalog, const std::map<std::int64_t, std::string>& values);

Json badges_to_json(const std::vector<Badge>& badges);

}  // namespace wcatalog
