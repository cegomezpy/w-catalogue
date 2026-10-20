#pragma once

#include <httplib.h>

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace wcatalog {

using Json = nlohmann::json;

// Responde con JSON UTF-8.
void send_json(httplib::Response& res, const Json& body, int status = 200);

// Error con la forma {"error": {"code": ..., "message": ...}}.
void send_error(httplib::Response& res, int status, const std::string& code, const std::string& message);

// 400 con errores por campo: {"error": {"code": "validation", "message": ..., "fields": {clave: mensaje}}}
void send_validation_error(httplib::Response& res, const std::string& message,
                           const std::map<std::string, std::string>& fields);

inline void send_bad_request(httplib::Response& res, const std::string& message) {
    send_error(res, 400, "validation", message);
}
inline void send_not_found(httplib::Response& res, const std::string& message) {
    send_error(res, 404, "not_found", message);
}
inline void send_conflict(httplib::Response& res, const std::string& message) {
    send_error(res, 409, "conflict", message);
}
inline void send_internal_error(httplib::Response& res, const std::string& message) {
    send_error(res, 500, "internal", message);
}

}  // namespace wcatalog
