#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>

#include "db/migrations.hpp"
#include "http/json_response.hpp"
#include "http/routes_settings.hpp"
#include "http/server.hpp"
#include "settings/settings_service.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct Env {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<SettingsService> settings;

    explicit Env(const char* name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        db = open_app_database(dir / "c.db", dir / "backups");
        settings = std::make_unique<SettingsService>(*db);
    }
    ~Env() {
        settings.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("Ajustes: una instalacion nueva devuelve los valores por defecto") {
    Env e("wcatalog-test-set1");
    Json s = e.settings->get();
    CHECK(s["tema"]["colorPrimario"] == "#1F3A5F");
    CHECK(s["tema"]["colorSecundario"] == "#E07A3F");
    CHECK(s["tema"]["colorTerciario"] == "#F4EFE6");
    CHECK(s["tipografia"]["fuenteTitulos"] == "Playfair Display");
    CHECK(s["tipografia"]["fuenteTexto"] == "Inter");
    CHECK(s["general"]["moneda"] == "USD");
    CHECK(s["contacto"]["email"] == "");
}

TEST_CASE("Ajustes: cambiar un color lo guarda normalizado a mayusculas") {
    Env e("wcatalog-test-set2");
    e.settings->update(Json{{"tema", {{"colorPrimario", "#aabbcc"}}}});
    CHECK(e.settings->get()["tema"]["colorPrimario"] == "#AABBCC");
    CHECK(e.settings->value("tema.colorPrimario") == "#AABBCC");
}

TEST_CASE("Ajustes: un color invalido se rechaza y conserva el valor anterior") {
    Env e("wcatalog-test-set3");
    for (const char* bad : {"azul", "#12", "#GGGGGG", "123456"}) {
        try {
            e.settings->update(Json{{"tema", {{"colorPrimario", bad}}}});
            FAIL("debia rechazar " << bad);
        } catch (const ValidationFailed& v) {
            CHECK(v.fields.count("tema.colorPrimario") == 1);
            CHECK(v.fields.at("tema.colorPrimario").find("#RRGGBB") != std::string::npos);
        }
    }
    CHECK(e.settings->get()["tema"]["colorPrimario"] == "#1F3A5F");
}

TEST_CASE("Ajustes: email invalido se rechaza; vacio es valido") {
    Env e("wcatalog-test-set4");
    CHECK_THROWS_AS(e.settings->update(Json{{"contacto", {{"email", "sin-arroba"}}}}), ValidationFailed);
    CHECK_NOTHROW(e.settings->update(Json{{"contacto", {{"email", ""}}}}));
    CHECK_NOTHROW(e.settings->update(Json{{"contacto", {{"email", "ana@example.com"}}}}));
    CHECK(e.settings->get()["contacto"]["email"] == "ana@example.com");
}

TEST_CASE("Ajustes: contacto parcial guarda solo lo enviado") {
    Env e("wcatalog-test-set5");
    e.settings->update(Json{{"contacto", {{"nombre", "Ana Garc\xC3\xAD" "a"}, {"whatsapp", "+57 300 1234567"}}}});
    Json c = e.settings->get()["contacto"];
    CHECK(c["nombre"] == "Ana Garc\xC3\xAD" "a");
    CHECK(c["whatsapp"] == "+57 300 1234567");
    CHECK(c["telefono"] == "");
    CHECK(c["email"] == "");
}

TEST_CASE("Ajustes: moneda en mayusculas y de 3 letras") {
    Env e("wcatalog-test-set6");
    e.settings->update(Json{{"general", {{"moneda", "eur"}}}});
    CHECK(e.settings->value("general.moneda") == "EUR");
    CHECK_THROWS_AS(e.settings->update(Json{{"general", {{"moneda", "dolares"}}}}), ValidationFailed);
}

TEST_CASE("Ajustes: claves desconocidas y tipos incorrectos se rechazan") {
    Env e("wcatalog-test-set7");
    CHECK_THROWS_AS(e.settings->update(Json{{"tema", {{"inventado", "x"}}}}), ValidationFailed);
    CHECK_THROWS_AS(e.settings->update(Json{{"tema", {{"colorPrimario", 5}}}}), ValidationFailed);
    CHECK_THROWS_AS(e.settings->update(Json::array()), ValidationFailed);
}

TEST_CASE("Ajustes: un parche con un campo invalido no escribe ninguno") {
    Env e("wcatalog-test-set8");
    CHECK_THROWS_AS(e.settings->update(Json{{"tema", {{"colorPrimario", "#112233"}, {"colorSecundario", "mal"}}}}),
                    ValidationFailed);
    CHECK(e.settings->get()["tema"]["colorPrimario"] == "#1F3A5F");
}

TEST_CASE("Ajustes: la fuente elegida debe existir si hay validador") {
    Env e("wcatalog-test-set9");
    e.settings->set_font_validator([](const std::string& f) { return f == "Inter"; });
    CHECK_NOTHROW(e.settings->update(Json{{"tipografia", {{"fuenteTexto", "Inter"}}}}));
    CHECK_THROWS_AS(e.settings->update(Json{{"tipografia", {{"fuenteTexto", "Zzz"}}}}), ValidationFailed);
}

TEST_CASE("Ajustes: reset vuelve al valor por defecto") {
    Env e("wcatalog-test-set10");
    e.settings->update(Json{{"tipografia", {{"fuenteTitulos", "Poppins"}}}});
    e.settings->reset(SettingsService::kFuenteTitulos);
    CHECK(e.settings->value(SettingsService::kFuenteTitulos) == "Playfair Display");
}

TEST_CASE("API /api/ajustes: GET y PUT; un PUT invalido no modifica nada") {
    Env e("wcatalog-test-set11");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_settings_routes(server.raw(), *e.settings);
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto get = cli.Get("/api/ajustes");
    REQUIRE(get);
    CHECK(get->status == 200);
    CHECK(Json::parse(get->body)["tema"]["colorPrimario"] == "#1F3A5F");

    auto ok = cli.Put("/api/ajustes", Json{{"tema", {{"colorPrimario", "#112233"}}}}.dump(), "application/json");
    REQUIRE(ok);
    CHECK(ok->status == 200);
    CHECK(Json::parse(ok->body)["tema"]["colorPrimario"] == "#112233");

    auto bad = cli.Put("/api/ajustes",
                       Json{{"tema", {{"colorPrimario", "#445566"}, {"colorTerciario", "nada"}}}, {"contacto", {{"email", "x"}}}}.dump(),
                       "application/json");
    REQUIRE(bad);
    CHECK(bad->status == 400);
    Json err = Json::parse(bad->body)["error"];
    CHECK(err["code"] == "validation");
    CHECK(err["fields"].contains("tema.colorTerciario"));
    CHECK(err["fields"].contains("contacto.email"));
    CHECK_FALSE(err["fields"].contains("tema.colorPrimario"));

    auto after = cli.Get("/api/ajustes");
    REQUIRE(after);
    CHECK(Json::parse(after->body)["tema"]["colorPrimario"] == "#112233");  // no cambio con el PUT invalido

    auto junk = cli.Put("/api/ajustes", "{no es json", "application/json");
    REQUIRE(junk);
    CHECK(junk->status == 400);
    server.stop();
}
