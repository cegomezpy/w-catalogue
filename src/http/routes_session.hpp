#pragma once

#include <httplib.h>

#include <chrono>

#include "runtime/lifecycle.hpp"

namespace wcatalog {

// GET /api/session/events: canal SSE de presencia. Mientras la conexión sigue abierta,
// la ventana se considera abierta; el servidor envía un keep-alive periódico.
void register_session_routes(httplib::Server& server, Lifecycle& lifecycle,
                              std::chrono::milliseconds keepalive = std::chrono::seconds(5));

}  // namespace wcatalog
