#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "db/database.hpp"
#include "db/migrations.hpp"
#include "http/json_response.hpp"
#include "http/routes_core.hpp"
#include "http/server.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct Fixture {
    fs::path dir;
    fs::path web;
    std::unique_ptr<Database> db;
    std::unique_ptr<HttpServer> server;
    int port = 0;

    explicit Fixture(const char* name, bool start = true) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        web = dir / "web";
        fs::create_directories(web / "css");
        std::ofstream(web / "index.html", std::ios::binary) << "<html><body>Inicio</body></html>";
        std::ofstream(web / "css" / "app.css", std::ios::binary) << "body{color:red}";

        db = open_app_database(dir / "catalog.db", dir / "backups");
        server = std::make_unique<HttpServer>(web);

        AppContext ctx;
        ctx.version = "9.9.9";
        ctx.data_dir = dir / "data";
        ctx.db = db.get();
        register_core_routes(server->raw(), ctx);

        server->raw().Get("/api/boom", [](const httplib::Request&, httplib::Response&) {
            throw std::runtime_error("fallo forzado");
        });
        server->raw().Get("/api/acentos", [](const httplib::Request&, httplib::Response& res) {
            send_json(res, Json{{"texto", "Se\xC3\xB1or Garc\xC3\xAD" "a - caf\xC3\xA9"}});
        });
        server->raw().Get("/api/conflicto", [](const httplib::Request&, httplib::Response& res) {
            send_conflict(res, "ya existe");
        });

        if (start) port = server->start();
    }

    ~Fixture() {
        server->stop();
        server.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    httplib::Client client() const { return httplib::Client("127.0.0.1", port); }
};

}  // namespace

TEST_CASE("el servidor escucha en un puerto libre de loopback") {
    Fixture f("wcatalog-test-http1");
    CHECK(f.port > 0);
    auto cli = f.client();
    auto res = cli.Get("/api/health");
    REQUIRE(res);
    CHECK(res->status == 200);
}

TEST_CASE("un Host ajeno recibe 403 con error JSON") {
    Fixture f("wcatalog-test-http2");
    auto cli = f.client();

    auto bad = cli.Get("/api/health", httplib::Headers{{"Host", "evil.example.com"}});
    REQUIRE(bad);
    CHECK(bad->status == 403);
    CHECK(Json::parse(bad->body)["error"]["code"] == "forbidden_host");

    auto wrong_port = cli.Get("/api/health", httplib::Headers{{"Host", "127.0.0.1:1"}});
    REQUIRE(wrong_port);
    CHECK(wrong_port->status == 403);

    auto localhost = cli.Get("/api/health", httplib::Headers{{"Host", "localhost:" + std::to_string(f.port)}});
    REQUIRE(localhost);
    CHECK(localhost->status == 200);

    // tampoco se sirven archivos estaticos a un Host ajeno
    auto page = cli.Get("/", httplib::Headers{{"Host", "evil.example.com"}});
    REQUIRE(page);
    CHECK(page->status == 403);
}

TEST_CASE("una ruta /api inexistente devuelve 404 JSON") {
    Fixture f("wcatalog-test-http3");
    auto cli = f.client();
    auto res = cli.Get("/api/no-existe");
    REQUIRE(res);
    CHECK(res->status == 404);
    CHECK(res->get_header_value("Content-Type").find("application/json") == 0);
    CHECK(Json::parse(res->body)["error"]["code"] == "not_found");
}

TEST_CASE("una excepcion produce 500 JSON y el servidor sigue respondiendo") {
    Fixture f("wcatalog-test-http4");
    auto cli = f.client();
    auto boom = cli.Get("/api/boom");
    REQUIRE(boom);
    CHECK(boom->status == 500);
    CHECK(Json::parse(boom->body)["error"]["code"] == "internal");

    auto health = cli.Get("/api/health");
    REQUIRE(health);
    CHECK(health->status == 200);
}

TEST_CASE("los helpers de error usan el codigo HTTP coherente") {
    Fixture f("wcatalog-test-http5");
    auto cli = f.client();
    auto res = cli.Get("/api/conflicto");
    REQUIRE(res);
    CHECK(res->status == 409);
    CHECK(Json::parse(res->body)["error"]["code"] == "conflict");
}

TEST_CASE("GET /api/health responde status ok") {
    Fixture f("wcatalog-test-http6");
    auto cli = f.client();
    auto res = cli.Get("/api/health");
    REQUIRE(res);
    CHECK(Json::parse(res->body) == Json{{"status", "ok"}});
}

TEST_CASE("GET /api/info devuelve version, dataDir y schemaVersion") {
    Fixture f("wcatalog-test-http7");
    auto cli = f.client();
    auto res = cli.Get("/api/info");
    REQUIRE(res);
    CHECK(res->status == 200);
    Json body = Json::parse(res->body);
    CHECK(body["version"] == "9.9.9");
    CHECK(body["dataDir"].get<std::string>() == to_utf8(f.dir / "data"));
    CHECK(body["schemaVersion"] == latest_version(app_migrations()));
}

TEST_CASE("el texto con tildes y ñ llega intacto") {
    Fixture f("wcatalog-test-http8");
    auto cli = f.client();
    auto res = cli.Get("/api/acentos");
    REQUIRE(res);
    CHECK(res->get_header_value("Content-Type").find("charset=utf-8") != std::string::npos);
    CHECK(Json::parse(res->body)["texto"] == "Se\xC3\xB1or Garc\xC3\xAD" "a - caf\xC3\xA9");
}

TEST_CASE("/ devuelve index.html y los css llevan text/css") {
    Fixture f("wcatalog-test-http9");
    auto cli = f.client();

    auto index = cli.Get("/");
    REQUIRE(index);
    CHECK(index->status == 200);
    CHECK(index->body.find("Inicio") != std::string::npos);
    CHECK(index->get_header_value("Content-Type").find("text/html") == 0);

    auto css = cli.Get("/css/app.css");
    REQUIRE(css);
    CHECK(css->status == 200);
    CHECK(css->get_header_value("Content-Type").find("text/css") == 0);
}

TEST_CASE("un archivo estatico inexistente da 404") {
    Fixture f("wcatalog-test-http10");
    auto cli = f.client();
    auto res = cli.Get("/nada.js");
    REQUIRE(res);
    CHECK(res->status == 404);
}

TEST_CASE("no se puede salir de web/ con rutas relativas") {
    Fixture f("wcatalog-test-http11");
    std::ofstream(f.dir / "secreto.txt") << "secreto";
    auto cli = f.client();
    auto res = cli.Get("/../secreto.txt");
    REQUIRE(res);
    CHECK(res->status == 404);
}

TEST_CASE("start falla con un mensaje claro si falta la carpeta web") {
    HttpServer server(fs::temp_directory_path() / "wcatalog-no-existe-web");
    CHECK_THROWS_AS(server.start(), std::runtime_error);
}
