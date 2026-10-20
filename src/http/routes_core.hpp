#pragma once

#include <httplib.h>

#include "app_context.hpp"

namespace wcatalog {

// GET /api/health y GET /api/info
void register_core_routes(httplib::Server& server, const AppContext& ctx);

}  // namespace wcatalog
