#include "http/routes_settings.hpp"

#include "http/json_response.hpp"

namespace wcatalog {

void register_settings_routes(httplib::Server& server, SettingsService& settings) {
    server.Get("/api/ajustes", [&settings](const httplib::Request&, httplib::Response& res) {
        send_json(res, settings.get());
    });

    server.Put("/api/ajustes", [&settings](const httplib::Request& req, httplib::Response& res) {
        Json body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            send_bad_request(res, "El cuerpo no es un JSON válido");
            return;
        }
        try {
            send_json(res, settings.update(body));
        } catch (const ValidationFailed& e) {
            send_validation_error(res, e.what(), e.fields);
        }
    });
}

}  // namespace wcatalog
