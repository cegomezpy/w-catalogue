#include "http/routes_photos.hpp"

#include <fstream>
#include <sstream>

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

void register_photo_routes(httplib::Server& server, PhotoService& photos) {
    server.Post(R"(/api/casas/(\d+)/fotos)", [&photos](const httplib::Request& req, httplib::Response& res) {
        if (!req.form.has_file("archivo")) {
            send_bad_request(res, "Falta la foto (campo \"archivo\")");
            return;
        }
        const httplib::FormData file = req.form.get_file("archivo");
        run_guarded(res, [&] {
            send_json(res,
                      photos.upload(std::stoll(req.matches[1]),
                                    reinterpret_cast<const std::uint8_t*>(file.content.data()), file.content.size()),
                      201);
        });
    });

    server.Put(R"(/api/casas/(\d+)/fotos/orden)", [&photos](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] {
            const Json ids = body.contains("ids") ? body["ids"] : Json(nullptr);
            send_json(res, photos.reorder(std::stoll(req.matches[1]), ids));
        });
    });

    server.Put(R"(/api/fotos/(\d+))", [&photos](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, photos.set_caption(std::stoll(req.matches[1]), body)); });
    });

    server.Delete(R"(/api/fotos/(\d+))", [&photos](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            photos.remove(std::stoll(req.matches[1]));
            send_json(res, Json{{"status", "ok"}});
        });
    });

    // Solo se sirven nombres con la forma exacta <id>_opt.jpg / <id>_min.jpg dentro de data/fotos/<casa>/.
    server.Get(R"(/media/fotos/([0-9]+)/([A-Za-z0-9._-]+))",
               [&photos](const httplib::Request& req, httplib::Response& res) {
                   const std::filesystem::path p = photos.media_file(req.matches[1], req.matches[2]);
                   if (p.empty()) {
                       res.status = 404;
                       return;
                   }
                   std::ifstream in(p, std::ios::binary);
                   std::ostringstream os;
                   os << in.rdbuf();
                   // el nombre incluye el id de la foto y no se reutiliza: la caché puede ser eterna
                   res.set_header("Cache-Control", "max-age=31536000, immutable");
                   res.set_content(os.str(), "image/jpeg");
               });

    // cualquier otra ruta bajo /media/fotos/ (con "..", subcarpetas, etc.) se rechaza
    server.Get(R"(/media/fotos/.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 404; });
}

}  // namespace wcatalog
