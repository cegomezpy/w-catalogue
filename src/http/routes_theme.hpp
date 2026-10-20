#pragma once

#include <httplib.h>

#include <functional>
#include <string>

#include "fonts/font_library.hpp"
#include "settings/settings_service.hpp"

namespace wcatalog {

// GET /api/tema.css (sin caché) y POST /api/sistema/abrir-enlace.
// `opener` abre una URL en el navegador predeterminado; se inyecta para poder probarlo.
using UrlOpener = std::function<bool(const std::string& url)>;

void register_theme_routes(httplib::Server& server, const SettingsService& settings, const FontLibrary& fonts);
void register_system_routes(httplib::Server& server, UrlOpener opener);

// Solo se aceptan URL https:// bien formadas.
bool is_safe_https_url(const std::string& url);

}  // namespace wcatalog
