#pragma once

#include <httplib.h>

#include "photos/photo_service.hpp"

namespace wcatalog {

// POST /api/casas/{id}/fotos (multipart, campo "archivo"), PUT /api/casas/{id}/fotos/orden,
// PUT/DELETE /api/fotos/{id}, GET /media/fotos/{casa_id}/{archivo}
void register_photo_routes(httplib::Server& server, PhotoService& photos);

}  // namespace wcatalog
