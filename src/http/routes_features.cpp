#include "http/routes_features.hpp"

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

void register_feature_routes(httplib::Server& server, FeatureService& features) {
    server.Get("/api/caracteristicas", [&features](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            Json arr = Json::array();
            for (const Feature& f : features.list(req.has_param("activas"))) arr.push_back(features.to_json(f));
            send_json(res, arr);
        });
    });

    server.Post("/api/caracteristicas", [&features](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, features.to_json(features.create(body)), 201); });
    });

    // antes que /{id}: "orden" no es un número, pero así queda explícito
    server.Put("/api/caracteristicas/orden", [&features](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] {
            if (!body.contains("ids") || !body["ids"].is_array()) {
                throw ValidationFailed({{"ids", "Se esperaba una lista de identificadores"}});
            }
            std::vector<std::int64_t> ids;
            for (const Json& v : body["ids"]) {
                if (!v.is_number_integer()) throw ValidationFailed({{"ids", "Los identificadores deben ser enteros"}});
                ids.push_back(v.get<std::int64_t>());
            }
            features.reorder(ids);
            Json arr = Json::array();
            for (const Feature& f : features.list()) arr.push_back(features.to_json(f));
            send_json(res, arr);
        });
    });

    server.Get(R"(/api/caracteristicas/(\d+))", [&features](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] { send_json(res, features.to_json(features.get(std::stoll(req.matches[1])))); });
    });

    server.Put(R"(/api/caracteristicas/(\d+))", [&features](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] {
            send_json(res, features.to_json(features.update(std::stoll(req.matches[1]), body)));
        });
    });

    server.Delete(R"(/api/caracteristicas/(\d+))", [&features](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            const std::int64_t id = std::stoll(req.matches[1]);
            const int affected = features.usage_count(id);
            features.remove(id);
            send_json(res, Json{{"status", "ok"}, {"casas_afectadas", affected}});
        });
    });
}

}  // namespace wcatalog
