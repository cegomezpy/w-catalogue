#include "http/json_response.hpp"

namespace wcatalog {

void send_json(httplib::Response& res, const Json& body, int status) {
    res.status = status;
    // replace: un texto con bytes inválidos no debe convertir la respuesta en un 500
    res.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json; charset=utf-8");
}

void send_error(httplib::Response& res, int status, const std::string& code, const std::string& message) {
    send_json(res, Json{{"error", {{"code", code}, {"message", message}}}}, status);
}

void send_validation_error(httplib::Response& res, const std::string& message,
                           const std::map<std::string, std::string>& fields) {
    Json f = Json::object();
    for (const auto& [k, v] : fields) f[k] = v;
    send_json(res, Json{{"error", {{"code", "validation"}, {"message", message}, {"fields", f}}}}, 400);
}

}  // namespace wcatalog
