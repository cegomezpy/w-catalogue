#pragma once

#include <httplib.h>

#include "fonts/font_library.hpp"

namespace wcatalog {

// GET /api/fuentes, POST /api/fuentes/analizar, POST /api/fuentes, DELETE /api/fuentes/{id}
// y GET /media/fuentes/{incluidas|importadas}/{archivo}
void register_font_routes(httplib::Server& server, FontLibrary& library);

}  // namespace wcatalog
