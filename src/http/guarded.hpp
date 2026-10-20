#pragma once

#include <httplib.h>

#include <functional>

namespace wcatalog {

// Ejecuta un manejador de ruta y traduce los errores de dominio a respuestas JSON:
// ValidationFailed -> 400 con `fields`, PhotoRejected -> 400, NotFoundError -> 404, ConflictError -> 409.
void run_guarded(httplib::Response& res, const std::function<void()>& fn);

}  // namespace wcatalog
