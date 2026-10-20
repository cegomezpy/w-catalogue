#pragma once

#include <httplib.h>

#include "houses/house_service.hpp"

namespace wcatalog {

// GET/POST /api/casas, GET/PUT/DELETE /api/casas/{id}, POST /api/casas/previsualizar-badges
void register_house_routes(httplib::Server& server, HouseService& houses);

}  // namespace wcatalog
