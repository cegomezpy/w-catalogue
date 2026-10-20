#pragma once

#include <string>

namespace wcatalog {

// Minúsculas y sin diacríticos, para búsquedas ("Peña" -> "pena", "VEDADO" -> "vedado").
// Cubre el rango latino (Latin-1 y Latin Extended-A); el resto del texto se conserva.
std::string fold_text(const std::string& utf8);

}  // namespace wcatalog
