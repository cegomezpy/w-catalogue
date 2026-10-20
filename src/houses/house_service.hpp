#pragma once

#include <cstdint>
#include <string>

#include "db/database.hpp"
#include "domain/errors.hpp"
#include "houses/feature_service.hpp"
#include "settings/settings_service.hpp"

namespace wcatalog {

class PhotoService;

// Registro de casas: validación, persistencia (datos fijos + valores de características en una
// sola transacción), búsqueda sin tildes y detalle con badges resueltos.
class HouseService {
public:
    HouseService(Database& db, SettingsService& settings, FeatureService& features);

    // Resumen por casa, de la más recientemente modificada a la más antigua.
    // q: texto buscado en título, barrio y municipio (sin tildes ni mayúsculas); estado: filtro opcional.
    Json list(const std::string& q = "", const std::string& estado = "") const;

    // Casa completa + `caracteristicas` + `badges`. NotFoundError si no existe.
    Json get(std::int64_t id) const;

    // La casa completa (el PUT reemplaza todos los datos y valores). ValidationFailed con `fields`.
    Json create(const Json& payload);
    Json update(std::int64_t id, const Json& payload);

    // Elimina la casa y sus valores de características (en cascada).
    void remove(std::int64_t id);

    // Opcional: con él, el detalle incluye `fotos`, la lista `portada_min` y eliminar una casa borra sus fotos.
    void set_photo_service(PhotoService* photos) { photos_ = photos; }

    // Badges que tendría una casa con estos valores (sin guardar nada): [{caracteristica_id, valor}].
    Json preview_badges(const Json& feature_values) const;

private:
    struct Parsed;
    Parsed parse(const Json& payload) const;
    void write_values(std::int64_t house_id, const Parsed& p);
    Json row_to_json(Statement& st) const;

    Database& db_;
    SettingsService& settings_;
    FeatureService& features_;
    PhotoService* photos_ = nullptr;
};

}  // namespace wcatalog
