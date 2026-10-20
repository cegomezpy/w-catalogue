#pragma once

#include <cstdint>
#include <vector>

#include "db/database.hpp"
#include "domain/errors.hpp"
#include "houses/feature.hpp"

namespace wcatalog {

// Catálogo de características: alta, edición, reordenación, desactivación y eliminación,
// con las reglas de integridad que protegen los valores de las casas.
class FeatureService {
public:
    explicit FeatureService(Database& db);

    // Ordenadas por `orden`. Con only_active solo las activas.
    std::vector<Feature> list(bool only_active = false) const;
    Feature get(std::int64_t id) const;  // NotFoundError

    // Casas que tienen un valor para la característica.
    int usage_count(std::int64_t id) const;

    // Payload: {etiqueta, categoria, tipo, opciones, unidad, icono, badge: {visible, texto, rol}, activa}.
    // ValidationFailed si hay campos inválidos.
    Feature create(const Json& payload);

    // Aplica sobre la característica solo los campos presentes en el payload.
    // ConflictError si cambia el tipo con valores guardados o quita una opción en uso.
    Feature update(std::int64_t id, const Json& payload);

    // ConflictError si es predefinida. Borra en cascada los valores de las casas.
    void remove(std::int64_t id);

    // Nuevo orden: `ids` pasan a ser las primeras, en ese orden; el resto sigue detrás.
    void reorder(const std::vector<std::int64_t>& ids);

    Json to_json(const Feature& f, int usage = -1) const;

private:
    Feature parse(const Feature& base, const Json& payload, bool is_create) const;
    std::string unique_key(const std::string& label) const;
    int option_usage(const Feature& f, const std::string& option) const;
    static Feature read_row(Statement& st);

    Database& db_;
};

}  // namespace wcatalog
