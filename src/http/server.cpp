#include "http/server.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "http/json_response.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

bool is_api_path(const std::string& path) { return path == "/api" || path.rfind("/api/", 0) == 0; }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

HttpServer::HttpServer(fs::path web_dir) : web_dir_(std::move(web_dir)) {
    // Mitiga DNS rebinding: solo se atiende a quien dice llamarse como el propio servidor.
    server_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (host_allowed(req.get_header_value("Host"))) return httplib::Server::HandlerResponse::Unhandled;
        send_error(res, 403, "forbidden_host", "Encabezado Host no permitido");
        return httplib::Server::HandlerResponse::Handled;
    });

    // Errores sin cuerpo bajo /api/ (ruta inexistente, método no permitido...) en formato común.
    server_.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (!is_api_path(req.path) || !res.body.empty()) return;
        switch (res.status) {
            case 404: send_error(res, 404, "not_found", "Ruta no encontrada: " + req.path); break;
            case 405: send_error(res, 405, "method_not_allowed", "Método no permitido"); break;
            default: send_error(res, res.status, "error", "Error HTTP " + std::to_string(res.status)); break;
        }
    });

    server_.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string what = "excepción desconocida";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        log().error("Excepción en " + req.method + " " + req.path + ": " + what);
        send_internal_error(res, "Error interno del servidor");
    });
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::host_allowed(const std::string& host) const {
    const std::string h = lower(host);
    const std::string suffix = ":" + std::to_string(port_.load());
    return h == "127.0.0.1" + suffix || h == "localhost" + suffix;
}

int HttpServer::start() {
    // Sin caché para que los cambios en web/ se vean al recargar
    if (!server_.set_mount_point("/", to_utf8(web_dir_), {{"Cache-Control", "no-cache"}})) {
        throw std::runtime_error("No se encontró la carpeta de la interfaz web: " + to_utf8(web_dir_));
    }
    int port = server_.bind_to_any_port("127.0.0.1");
    if (port <= 0) throw std::runtime_error("No se pudo abrir un puerto local para el servidor");
    port_ = port;
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
    log().info("Servidor escuchando en 127.0.0.1:" + std::to_string(port));
    return port;
}

void HttpServer::stop() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
}

}  // namespace wcatalog
