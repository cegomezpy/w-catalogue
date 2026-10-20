#pragma once

#include <string>

namespace wcatalog {

// Muestra un cuadro de diálogo con el error (texto en UTF-8) y lo registra en el log.
void show_fatal_error(const std::string& message);

}  // namespace wcatalog
