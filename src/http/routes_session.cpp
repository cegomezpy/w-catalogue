#include "http/routes_session.hpp"

#include <thread>

namespace wcatalog {

void register_session_routes(httplib::Server& server, Lifecycle& lifecycle, std::chrono::milliseconds keepalive) {
    server.Get("/api/session/events", [&server, &lifecycle, keepalive](const httplib::Request&, httplib::Response& res) {
        lifecycle.connection_opened();
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");

        res.set_chunked_content_provider(
            "text/event-stream",
            [&server, keepalive](size_t, httplib::DataSink& sink) {
                static const std::string hello = "retry: 1000\n: conectado\n\n";
                static const std::string ping = ": keep-alive\n\n";
                if (!sink.write(hello.data(), hello.size())) return false;

                auto last = std::chrono::steady_clock::now();
                while (server.is_running() && sink.is_writable()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (std::chrono::steady_clock::now() - last >= keepalive) {
                        if (!sink.write(ping.data(), ping.size())) break;
                        last = std::chrono::steady_clock::now();
                    }
                }
                sink.done();
                return true;
            },
            // se llama al terminar el flujo, también si el cliente cortó
            [&lifecycle](bool) { lifecycle.connection_closed(); });
    });
}

}  // namespace wcatalog
