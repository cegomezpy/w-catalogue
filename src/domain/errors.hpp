#pragma once

#include <map>
#include <stdexcept>
#include <string>

namespace wcatalog {

// Uno o más campos no pasaron la validación; `fields` mapea el campo al mensaje.
struct ValidationFailed : std::runtime_error {
    explicit ValidationFailed(std::map<std::string, std::string> f)
        : std::runtime_error("Hay campos no válidos"), fields(std::move(f)) {}
    std::map<std::string, std::string> fields;
};

struct NotFoundError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// La operación choca con el estado actual de los datos (HTTP 409).
struct ConflictError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace wcatalog
