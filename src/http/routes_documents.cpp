#include "http/routes_documents.hpp"

#include <fstream>
#include <sstream>

#include <cctype>

#include "documents/document_text.hpp"
#include "documents/template_engine.hpp"
#include "http/guarded.hpp"
#include "http/json_response.hpp"
#include "runtime/log.hpp"
#include "runtime/shell_open.hpp"

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

void register_format_routes(httplib::Server& server, const FormatCatalog& formats) {
    server.Get("/api/formatos", [&formats](const httplib::Request&, httplib::Response& res) {
        send_json(res, formats.to_json());
    });
}

void register_document_routes(httplib::Server& server, DocumentService& documents) {
    server.Get("/api/documentos", [&documents](const httplib::Request&, httplib::Response& res) {
        run_guarded(res, [&] { send_json(res, documents.list()); });
    });

    server.Post("/api/documentos", [&documents](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, documents.create(body), 201); });
    });

    server.Get(R"(/api/documentos/(\d+))", [&documents](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] { send_json(res, documents.get(std::stoll(req.matches[1]))); });
    });

    server.Put(R"(/api/documentos/(\d+))", [&documents](const httplib::Request& req, httplib::Response& res) {
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, documents.update(std::stoll(req.matches[1]), body)); });
    });

    server.Delete(R"(/api/documentos/(\d+))", [&documents](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] {
            documents.remove(std::stoll(req.matches[1]));
            send_json(res, Json{{"status", "ok"}});
        });
    });

    server.Post(R"(/api/documentos/(\d+)/portada)", [&documents](const httplib::Request& req, httplib::Response& res) {
        const std::int64_t id = std::stoll(req.matches[1]);
        if (req.form.has_file("archivo")) {
            const httplib::FormData file = req.form.get_file("archivo");
            run_guarded(res, [&] {
                send_json(res, documents.set_cover_upload(
                                   id, reinterpret_cast<const std::uint8_t*>(file.content.data()), file.content.size()));
            });
            return;
        }
        Json body;
        if (!parse_body(req, res, body)) return;
        run_guarded(res, [&] { send_json(res, documents.set_cover_photo(id, body)); });
    });

    server.Get(R"(/api/casas/(\d+)/documentos)", [&documents](const httplib::Request& req, httplib::Response& res) {
        run_guarded(res, [&] { send_json(res, documents.for_house(std::stoll(req.matches[1]))); });
    });

    server.Get(R"(/media/documentos/(\d+)/portada_opt\.jpg)",
               [&documents](const httplib::Request& req, httplib::Response& res) {
                   const std::filesystem::path p = documents.cover_file(std::stoll(req.matches[1]));
                   if (p.empty()) {
                       res.status = 404;
                       return;
                   }
                   std::ifstream in(p, std::ios::binary);
                   std::ostringstream os;
                   os << in.rdbuf();
                   // la portada se reemplaza conservando el nombre: el navegador debe revalidar
                   res.set_header("Cache-Control", "no-cache");
                   res.set_content(os.str(), "image/jpeg");
               });

    server.Get(R"(/media/documentos/.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 404; });
}

void register_export_routes(httplib::Server& server, DocumentExporter& exporter) {
    server.Post(R"(/api/documentos/(\d+)/exportar)", [&exporter](const httplib::Request& req, httplib::Response& res) {
        Json body = Json::object();
        if (!req.body.empty()) {
            body = Json::parse(req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) {
                send_bad_request(res, "El cuerpo no es un JSON válido");
                return;
            }
        }
        run_guarded(res, [&] {
            try {
                send_json(res, exporter.export_document(std::stoll(req.matches[1]), body));
            } catch (const ExportBusyError& e) {
                send_error(res, 409, "exportacion_en_curso", e.what());
            }
        });
    });
}

void register_open_path_routes(httplib::Server& server, std::filesystem::path outputs_dir, PathOpener opener) {
    server.Post("/api/sistema/abrir-ruta",
                [outputs_dir = std::move(outputs_dir), opener = std::move(opener)](const httplib::Request& req,
                                                                                    httplib::Response& res) {
                    Json body = Json::parse(req.body, nullptr, false);
                    if (body.is_discarded() || !body.is_object() || !body.contains("ruta") || !body["ruta"].is_string()) {
                        send_bad_request(res, "Falta la ruta");
                        return;
                    }
                    const std::filesystem::path path = std::filesystem::u8path(body["ruta"].get<std::string>());
                    std::error_code ec;
                    const bool is_dir = std::filesystem::is_directory(path, ec);
                    std::string ext = path.extension().string();
                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const bool allowed_file = std::filesystem::is_regular_file(path, ec) && (ext == ".html" || ext == ".pdf");
                    // dentro de data/salidas/, resueltas las rutas relativas y los enlaces
                    if (!(is_dir || allowed_file) || !path.is_absolute() || !is_path_within(outputs_dir, path)) {
                        send_bad_request(res, "Solo se pueden abrir carpetas, HTML y PDF de data/salidas/");
                        return;
                    }
                    if (!opener || !opener(path)) {
                        send_internal_error(res, "No se pudo abrir la ruta");
                        return;
                    }
                    send_json(res, Json{{"status", "ok"}});
                });
}

void register_render_routes(httplib::Server& server, const DocumentContextBuilder& builder,
                            const DocumentRenderer& renderer, const FormatCatalog& formats) {
    server.Get(R"(/render/(\d+)/([a-z0-9-]+))",
               [&builder, &renderer, &formats](const httplib::Request& req, httplib::Response& res) {
                   const auto format = formats.find(req.matches[2]);
                   if (!format) {
                       send_bad_request(res, "Formato desconocido: " + std::string(req.matches[2]));
                       return;
                   }
                   run_guarded(res, [&] {
                       try {
                           const DocumentSnapshot snap = builder.snapshot(std::stoll(req.matches[1]), RenderMode::Preview);
                           res.set_header("Cache-Control", "no-store");
                           res.set_content(renderer.render(DocumentContextBuilder::for_format(snap.context, *format)),
                                           "text/html; charset=utf-8");
                       } catch (const TemplateError& e) {
                           log().error(std::string("Vista previa: ") + e.what());
                           // la vista previa se muestra en un iframe: un error legible es mejor que un JSON
                           res.status = 500;
                           res.set_header("Cache-Control", "no-store");
                           res.set_content("<!DOCTYPE html><html lang=\"es\"><meta charset=\"utf-8\"><body style=\"font: 16px "
                                           "system-ui, sans-serif; padding: 24px\"><h2>No se pudo generar la vista previa</h2>"
                                           "<pre style=\"white-space: pre-wrap\">" + html_escape(e.what()) + "</pre></body></html>",
                                           "text/html; charset=utf-8");
                       }
                   });
               });
}

}  // namespace wcatalog
