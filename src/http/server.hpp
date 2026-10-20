#pragma once

#include <httplib.h>

#include <atomic>
#include <filesystem>
#include <thread>

namespace wcatalog {

// Servidor HTTP local: solo loopback, puerto libre, valida el encabezado Host,
// sirve web/ en "/" y responde errores JSON bajo /api/.
class HttpServer {
public:
    explicit HttpServer(std::filesystem::path web_dir);
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    ~HttpServer();

    // Para registrar rutas antes de start().
    httplib::Server& raw() { return server_; }

    // Escucha en 127.0.0.1 en un puerto libre y devuelve el puerto.
    int start();
    void stop();

    int port() const { return port_; }

private:
    bool host_allowed(const std::string& host) const;

    httplib::Server server_;
    std::filesystem::path web_dir_;
    std::atomic<int> port_{0};
    std::thread thread_;
};

}  // namespace wcatalog
