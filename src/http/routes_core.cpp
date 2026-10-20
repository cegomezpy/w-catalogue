#include "http/routes_core.hpp"

#include "db/database.hpp"
#include "http/json_response.hpp"
#include "runtime/paths.hpp"

namespace wcatalog {

void register_core_routes(httplib::Server& server, const AppContext& ctx) {
    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, Json{{"status", "ok"}});
    });

    server.Get("/api/info", [ctx](const httplib::Request&, httplib::Response& res) {
        send_json(res, Json{{"version", ctx.version},
                            {"dataDir", to_utf8(ctx.data_dir)},
                            {"schemaVersion", ctx.db ? ctx.db->user_version() : 0}});
    });
}

}  // namespace wcatalog
