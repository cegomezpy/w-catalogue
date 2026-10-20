#pragma once

#include <httplib.h>

#include "houses/feature_service.hpp"

namespace wcatalog {

// GET/POST /api/caracteristicas, GET/PUT/DELETE /api/caracteristicas/{id}, PUT /api/caracteristicas/orden
void register_feature_routes(httplib::Server& server, FeatureService& features);

}  // namespace wcatalog
