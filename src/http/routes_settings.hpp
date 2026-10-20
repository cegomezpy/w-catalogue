#pragma once

#include <httplib.h>

#include "settings/settings_service.hpp"

namespace wcatalog {

// GET/PUT /api/ajustes
void register_settings_routes(httplib::Server& server, SettingsService& settings);

}  // namespace wcatalog
