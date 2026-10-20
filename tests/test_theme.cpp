#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "db/migrations.hpp"
#include "fonts/font_library.hpp"
#include "http/json_response.hpp"
#include "http/routes_settings.hpp"
#include "http/routes_theme.hpp"
#include "http/server.hpp"
#include "settings/settings_service.hpp"
#include "settings/theme.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

const fs::path kFontsDir = fs::path(WCATALOG_SOURCE_DIR) / "fonts";

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

struct Env {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<SettingsService> settings;
    std::unique_ptr<FontLibrary> lib;

    explicit Env(const char* name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        db = open_app_database(dir / "c.db", dir / "backups");
        settings = std::make_unique<SettingsService>(*db);
        lib = std::make_unique<FontLibrary>(*db, *settings,
                                            FontPaths{kFontsDir, dir / "data" / "fuentes", dir / "data" / "tmp"});
        lib->load_manifest();
        settings->set_font_validator([this](const std::string& f) { return lib->family_exists(f); });
    }
    ~Env() {
        lib.reset();
        settings.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("Tema: contiene las 5 variables y los @font-face de las familias elegidas") {
    Env e("wcatalog-test-theme1");
    std::string css = build_theme_css(*e.settings, *e.lib, "/media/fuentes/");
    for (const char* v : {"--color-primario: #1F3A5F", "--color-secundario: #E07A3F", "--color-terciario: #F4EFE6",
                          "--fuente-titulos: \"Playfair Display\", serif", "--fuente-texto: \"Inter\", sans-serif"}) {
        CHECK(contains(css, v));
    }
    // 2 familias elegidas x 2 pesos = 4 @font-face; Poppins y Montserrat no se emiten
    size_t faces = 0;
    for (size_t p = css.find("@font-face"); p != std::string::npos; p = css.find("@font-face", p + 1)) ++faces;
    CHECK(faces == 4);
    CHECK(contains(css, "url(\"/media/fuentes/incluidas/PlayfairDisplay-Bold.ttf\")"));
    CHECK(contains(css, "font-weight: 700"));
    CHECK_FALSE(contains(css, "Poppins-"));
}

TEST_CASE("Tema: refleja un cambio de color y de fuente de inmediato") {
    Env e("wcatalog-test-theme2");
    e.settings->update(Json{{"tema", {{"colorPrimario", "#112233"}}}, {"tipografia", {{"fuenteTitulos", "Poppins"}}}});
    std::string css = build_theme_css(*e.settings, *e.lib, "/media/fuentes/");
    CHECK(contains(css, "--color-primario: #112233"));
    CHECK(contains(css, "--fuente-titulos: \"Poppins\", sans-serif"));
    CHECK(contains(css, "Poppins-Regular.ttf"));
    CHECK_FALSE(contains(css, "PlayfairDisplay"));
}

TEST_CASE("Tema: la URL base es parametrizable para la exportacion") {
    Env e("wcatalog-test-theme3");
    std::string css = build_theme_css(*e.settings, *e.lib, "assets/fuentes/", false);
    CHECK(contains(css, "url(\"assets/fuentes/Inter-Regular.ttf\")"));
    CHECK_FALSE(contains(css, "/media/"));
    CHECK_FALSE(contains(css, "incluidas/"));
}

TEST_CASE("Tema: una familia importada elegida incluye todas sus variantes") {
    Env e("wcatalog-test-theme4");
    // Lobos (5 letras como Inter) importada en dos pesos
    auto renamed = [&](const std::string& file) {
        std::string b = slurp(kFontsDir / file);
        auto wide = [](const std::string& s) { std::string w; for (char c : s) { w += '\0'; w += c; } return w; };
        for (size_t p = b.find(wide("Inter")); p != std::string::npos; p = b.find(wide("Inter"), p + 10)) b.replace(p, 10, wide("Lobos"));
        for (size_t p = b.find("Inter"); p != std::string::npos; p = b.find("Inter", p + 5)) b.replace(p, 5, "Lobos");
        return b;
    };
    for (auto [file, peso] : {std::pair<const char*, int>{"Inter-Regular.ttf", 400}, {"Inter-Bold.ttf", 700}}) {
        Json a = e.lib->analyze(renamed(file), file);
        e.lib->import_font(Json{{"token", a["token"]}, {"familia", "Lobos"}, {"peso", peso}, {"estilo", "normal"}});
    }
    e.settings->update(Json{{"tipografia", {{"fuenteTitulos", "Lobos"}}}});
    std::string css = build_theme_css(*e.settings, *e.lib, "/media/fuentes/");
    CHECK(contains(css, "font-family: \"Lobos\""));
    CHECK(contains(css, "/media/fuentes/importadas/lobos-400-normal.ttf"));
    CHECK(contains(css, "/media/fuentes/importadas/lobos-700-normal.ttf"));
}

TEST_CASE("Tema: si la familia elegida ya no existe se usa la de por defecto") {
    Env e("wcatalog-test-theme5");
    // se fuerza un valor huerfano directamente en la base (como si la fuente se hubiera borrado a mano)
    e.db->exec("INSERT INTO ajustes (clave, valor) VALUES ('tipografia.fuenteTitulos', 'Fantasma')");
    std::string css = build_theme_css(*e.settings, *e.lib, "/media/fuentes/");
    CHECK(contains(css, "--fuente-titulos: \"Playfair Display\", serif"));
    CHECK_FALSE(contains(css, "Fantasma"));
}

TEST_CASE("API tema.css: no-store y refleja cambios") {
    Env e("wcatalog-test-theme6");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_settings_routes(server.raw(), *e.settings);
    register_theme_routes(server.raw(), *e.settings, *e.lib);
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto r1 = cli.Get("/api/tema.css");
    REQUIRE(r1);
    CHECK(r1->status == 200);
    CHECK(r1->get_header_value("Content-Type").find("text/css") == 0);
    CHECK(r1->get_header_value("Cache-Control") == "no-store");
    CHECK(contains(r1->body, "--color-primario: #1F3A5F"));

    cli.Put("/api/ajustes", Json{{"tema", {{"colorPrimario", "#abcdef"}}}}.dump(), "application/json");
    auto r2 = cli.Get("/api/tema.css");
    REQUIRE(r2);
    CHECK(contains(r2->body, "--color-primario: #ABCDEF"));
    server.stop();
}

TEST_CASE("abrir-enlace: solo https; file: y http: devuelven 400") {
    Env e("wcatalog-test-theme7");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    std::vector<std::string> opened;
    register_system_routes(server.raw(), [&](const std::string& u) { opened.push_back(u); return true; });
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto post = [&](const Json& body) { return cli.Post("/api/sistema/abrir-enlace", body.dump(), "application/json"); };

    for (const char* bad : {"file:///C:/Windows/System32/calc.exe", "http://fonts.google.com", "ftp://x.com",
                            "javascript:alert(1)", "C:\\Windows\\notepad.exe", "https://", "https:///etc/passwd",
                            "https://a.com/ b", "HTTP://fonts.google.com"}) {
        auto r = post(Json{{"url", bad}});
        REQUIRE(r);
        CHECK_MESSAGE(r->status == 400, bad);
    }
    CHECK(post(Json{{"url", 5}})->status == 400);
    CHECK(post(Json::object())->status == 400);
    CHECK(opened.empty());

    auto ok = post(Json{{"url", "https://fonts.google.com"}});
    REQUIRE(ok);
    CHECK(ok->status == 200);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0] == "https://fonts.google.com");
    server.stop();
}
