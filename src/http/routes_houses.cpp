#include "http/routes_houses.hpp"

#include "http/guarded.hpp"
#include "http/json_response.hpp"

namespace wcatalog {

namespace {

bool parse_body(const httplib::Request& req, httplib::Response& res, Json& out) {
    out = Json::parse(req.body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        send_bad_request(res, "El cuerpo no es un JSON válido");
        return false;
    }
    return true;
}

}  // namespace

void register_house_routes(httplib::Server& server, HouseService& houses) {
    server.Get("/api/casas", [&houses](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            send_json(res, houses.list(req.get_param_value("q"), req.get_param_value("estado")));
        });
    });

    server.Post("/api/casas", [&houses](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, houses.create(body), 201); });
    });

    server.Post("/api/casas/previsualizar-badges", [&houses](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] {
            send_json(res, Json{{"badges", houses.preview_badges(body.value("caracteristicas", Json::array()))}});
        });
    });

    server.Get(R"(/api/casas/(\d+))", [&houses](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] { send_json(res, houses.get(std::stoll(req.matches[1]))); });
    });

    server.Put(R"(/api/casas/(\d+))", [&houses](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, houses.update(std::stoll(req.matches[1]), body)); });
    });

    server.Delete(R"(/api/casas/(\d+))", [&houses](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            houses.remove(std::stoll(req.matches[1]));
            send_json(res, Json{{"status", "ok"}});
        });
    });
}

}  // namespace wcatalog
