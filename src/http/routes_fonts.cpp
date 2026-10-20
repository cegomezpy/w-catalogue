#include "http/routes_fonts.hpp"

#include <fstream>
#include <sstream>

#include "http/json_response.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

void send_font_error(httplib::Response& res, const FontError& e) {
    switch (e.kind) {
        case FontError::Kind::Conflict: send_conflict(res, e.what()); break;
        case FontError::Kind::NotFound: send_not_found(res, e.what()); break;
        default: send_bad_request(res, e.what()); break;
    }
}

}  // namespace

void register_font_routes(httplib::Server& server, FontLibrary& library) {
    server.Get("/api/fuentes", [&library](const httplib::Request&, httplib::Response& res) {
        send_json(res, library.families_json());
    });

    server.Post("/api/fuentes/analizar", [&library](const httplib::Request& req, httplib::Response& res) {
        if (!req.form.has_file("archivo")) {
            send_bad_request(res, "Falta el archivo de la fuente (campo \"archivo\")");
            return;
        }
        const httplib::FormData file = req.form.get_file("archivo");
        try {
            send_json(res, library.analyze(file.content, file.filename));
        } catch (const FontError& e) {
            send_font_error(res, e);
        }
    });

    server.Post("/api/fuentes", [&library](const httplib::Request& req, httplib::Response& res) {
        Json body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            send_bad_request(res, "El cuerpo no es un JSON válido");
            return;
        }
        try {
            send_json(res, library.import_font(body), 201);
        } catch (const FontError& e) {
            send_font_error(res, e);
        }
    });

    server.Delete(R"(/api/fuentes/(\d+))", [&library](const httplib::Request& req, httplib::Response& res) {
        try {
            send_json(res, library.remove(std::stoll(req.matches[1])));
        } catch (const FontError& e) {
            send_font_error(res, e);
        }
    });

    server.Get(R"(/media/fuentes/(incluidas|importadas)/([A-Za-z0-9._-]+))",
               [&library](const httplib::Request& req, httplib::Response& res) {
                   const fs::path p = library.file_path(req.matches[1], req.matches[2]);
                   if (p.empty()) {
                       res.status = 404;
                       return;
                   }
                   std::ifstream in(p, std::ios::binary);
                   std::ostringstream os;
                   os << in.rdbuf();
                   const bool otf = p.extension() == ".otf";
                   res.set_header("Cache-Control", "no-cache");
                   res.set_content(os.str(), otf ? "font/otf" : "font/ttf");
               });
}

}  // namespace wcatalog
