#include "doctest.h"

#include <httplib.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

#include "http/routes_session.hpp"
#include "http/server.hpp"
#include "runtime/edge_launcher.hpp"
#include "runtime/instance.hpp"
#include "runtime/lifecycle.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using namespace wcatalog;

namespace {

bool wait_for(const std::function<bool()>& cond, std::chrono::milliseconds timeout) {
    auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return cond();
}

struct SessionFixture {
    fs::path dir;
    std::atomic<int> shutdowns{0};
    std::unique_ptr<Lifecycle> lifecycle;
    std::unique_ptr<HttpServer> server;
    int port = 0;

    explicit SessionFixture(const char* name, std::chrono::milliseconds grace, std::chrono::milliseconds startup) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir / "web");
        std::ofstream(dir / "web" / "index.html") << "x";
        lifecycle = std::make_unique<Lifecycle>(grace, startup, [this] { ++shutdowns; });
        server = std::make_unique<HttpServer>(dir / "web");
        register_session_routes(server->raw(), *lifecycle, 100ms);
        port = server->start();
        lifecycle->start();
    }
    ~SessionFixture() {
        lifecycle->stop();
        server->stop();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Abre el canal de presencia en un hilo; se cierra poniendo `close` a true.
    std::thread open_channel(std::atomic<bool>& close, std::atomic<bool>& connected) {
        return std::thread([this, &close, &connected] {
            httplib::Client cli("127.0.0.1", port);
            cli.set_read_timeout(10, 0);
            cli.Get("/api/session/events", [&](const char*, size_t) {
                connected = true;
                return !close.load();
            });
        });
    }
};

}  // namespace

TEST_CASE("Lifecycle: se apaga tras la gracia inicial si nadie se conecta") {
    std::atomic<int> n{0};
    Lifecycle lc(100ms, 200ms, [&] { ++n; });
    lc.start();
    CHECK(wait_for([&] { return n == 1; }, 2000ms));
}

TEST_CASE("Lifecycle: una conexion abierta impide el apagado") {
    std::atomic<int> n{0};
    Lifecycle lc(100ms, 200ms, [&] { ++n; });
    lc.start();
    lc.connection_opened();
    std::this_thread::sleep_for(500ms);
    CHECK(n == 0);
    lc.connection_closed();
    CHECK(wait_for([&] { return n == 1; }, 2000ms));
}

TEST_CASE("Lifecycle: reconectar dentro de la gracia cancela el apagado") {
    std::atomic<int> n{0};
    Lifecycle lc(400ms, 5000ms, [&] { ++n; });
    lc.start();
    lc.connection_opened();
    lc.connection_closed();
    std::this_thread::sleep_for(100ms);
    lc.connection_opened();
    std::this_thread::sleep_for(700ms);
    CHECK(n == 0);
    CHECK(lc.connections() == 1);
}

TEST_CASE("Lifecycle: el fin del proceso de la ventana aplica la gracia") {
    std::atomic<int> n{0};
    Lifecycle lc(100ms, 5000ms, [&] { ++n; });
    lc.start();
    lc.window_process_exited();
    CHECK(wait_for([&] { return n == 1; }, 2000ms));
}

TEST_CASE("Presencia SSE: el servidor se detiene tras cerrar la ultima conexion") {
    SessionFixture f("wcatalog-test-sse1", 300ms, 10000ms);
    std::atomic<bool> close{false}, connected{false};
    std::thread t = f.open_channel(close, connected);

    REQUIRE(wait_for([&] { return connected.load(); }, 3000ms));
    CHECK(f.lifecycle->connections() == 1);
    std::this_thread::sleep_for(500ms);
    CHECK(f.shutdowns == 0);

    close = true;
    t.join();
    CHECK(wait_for([&] { return f.lifecycle->connections() == 0; }, 3000ms));
    CHECK(wait_for([&] { return f.shutdowns == 1; }, 3000ms));
}

TEST_CASE("Presencia SSE: una recarga dentro de la gracia no apaga el servidor") {
    SessionFixture f("wcatalog-test-sse2", 1500ms, 10000ms);
    std::atomic<bool> close1{false}, connected1{false};
    std::thread t1 = f.open_channel(close1, connected1);
    REQUIRE(wait_for([&] { return connected1.load(); }, 3000ms));

    close1 = true;  // F5: se cae la conexion...
    t1.join();
    std::atomic<bool> close2{false}, connected2{false};
    std::thread t2 = f.open_channel(close2, connected2);  // ...y la pagina nueva se reconecta
    REQUIRE(wait_for([&] { return connected2.load(); }, 3000ms));

    std::this_thread::sleep_for(2000ms);
    CHECK(f.shutdowns == 0);
    CHECK(f.lifecycle->connections() == 1);

    close2 = true;
    t2.join();
}

TEST_CASE("SingleInstance: el segundo intento en la misma carpeta falla y lee el puerto") {
    fs::path dir = fs::temp_directory_path() / "wcatalog-test-inst1";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        SingleInstance first;
        REQUIRE(first.acquire(dir));
        first.publish_port(4321);

        SingleInstance second;
        CHECK_FALSE(second.acquire(dir));
        auto port = SingleInstance::read_port(dir);
        REQUIRE(port.has_value());
        CHECK(*port == 4321);
    }
    // al cerrar la primera se libera todo
    CHECK_FALSE(SingleInstance::read_port(dir).has_value());
    SingleInstance third;
    CHECK(third.acquire(dir));
    fs::remove_all(dir);
}

TEST_CASE("SingleInstance: carpetas distintas pueden convivir") {
    fs::path a = fs::temp_directory_path() / "wcatalog-test-inst-a";
    fs::path b = fs::temp_directory_path() / "wcatalog-test-inst-b";
    fs::create_directories(a);
    fs::create_directories(b);
    SingleInstance ia, ib;
    CHECK(ia.acquire(a));
    CHECK(ib.acquire(b));
    CHECK(SingleInstance::mutex_name(a) != SingleInstance::mutex_name(b));
    fs::remove_all(a);
    fs::remove_all(b);
}

TEST_CASE("Edge: la linea de comandos usa --app y un perfil propio") {
    std::wstring cmd = edge_command_line(L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
                                         "http://127.0.0.1:5000/", L"C:\\Users\\a\\AppData\\Local\\WCatalog\\edge-profile");
    CHECK(cmd.find(L"--app=http://127.0.0.1:5000/") != std::wstring::npos);
    CHECK(cmd.find(L"--user-data-dir=C:\\Users\\a\\AppData\\Local\\WCatalog\\edge-profile") != std::wstring::npos);
    CHECK(cmd.find(L"--no-first-run") != std::wstring::npos);
    CHECK(cmd.front() == L'"');
}

TEST_CASE("Edge: una ruta forzada invalida se trata como Edge no encontrado") {
    SetEnvironmentVariableA("WCATALOG_EDGE_PATH", "C:\\no\\existe\\msedge.exe");
    CHECK_FALSE(find_edge().has_value());
    SetEnvironmentVariableA("WCATALOG_EDGE_PATH", nullptr);
}

TEST_CASE("Edge: sin forzar la ruta, se localiza en esta maquina") {
    SetEnvironmentVariableA("WCATALOG_EDGE_PATH", nullptr);
    auto edge = find_edge();
    REQUIRE(edge.has_value());
    CHECK(edge->filename() == "msedge.exe");
}
