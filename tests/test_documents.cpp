#include "doctest.h"

#include <filesystem>
#include <fstream>

#include <httplib.h>

#include "documents/formats.hpp"
#include "documents/template_engine.hpp"
#include "http/routes_documents.hpp"
#include "http/server.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / name;
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& rel, const std::string& content) const {
        fs::create_directories((path / fs::u8path(rel)).parent_path());
        std::ofstream(path / fs::u8path(rel), std::ios::binary) << content;
    }
};

}  // namespace

TEST_CASE("Inja renderiza if, for e include") {
    TempDir t("wc_tpl_basic");
    t.write("parciales/_item.html", "<li>{{ n.nombre }}</li>");
    t.write("pagina.html",
            "{% if titulo %}<h1>{{ titulo }}</h1>{% endif %}"
            "{% if oculto %}<p>no</p>{% endif %}"
            "<ul>{% for n in nombres %}{% include \"parciales/_item.html\" %}{% endfor %}</ul>");
    TemplateEngine engine(t.path);
    nlohmann::json data = {{"titulo", "Casa en Miramar"}, {"oculto", nullptr}, {"nombres", nlohmann::json::array()}};
    data["nombres"].push_back({{"nombre", "Año"}});
    data["nombres"].push_back({{"nombre", "Bañera"}});
    const std::string html = engine.render("pagina.html", data);
    CHECK(html == "<h1>Casa en Miramar</h1><ul><li>Año</li><li>Bañera</li></ul>");
}

TEST_CASE("formatos: templates/formatos.json define los tres formatos") {
    const FormatCatalog cat = FormatCatalog::load(fs::path(WCATALOG_SOURCE_DIR) / "templates" / "formatos.json");
    REQUIRE(cat.all().size() == 3);
    auto ig = cat.find("instagram-4x5");
    REQUIRE(ig);
    CHECK(ig->name == "Instagram 4:5");
    CHECK(ig->width == 1080);
    CHECK(ig->height == 1350);
    CHECK(ig->max_badges == 8);
    auto ls = cat.find("landscape-16x9");
    REQUIRE(ls);
    CHECK(ls->width == 1920);
    CHECK(ls->height == 1080);
    auto mv = cat.find("movil-9x16");
    REQUIRE(mv);
    CHECK(mv->width == 1080);
    CHECK(mv->height == 1920);
    CHECK_FALSE(cat.find("desconocido"));
}

TEST_CASE("formatos: un archivo roto o un formato invalido da un error claro") {
    TempDir t("wc_fmt_bad");
    CHECK_THROWS_WITH_AS(FormatCatalog::load(t.path / "no-existe.json"), doctest::Contains("formatos"), std::runtime_error);
    t.write("a.json", "{no es json");
    CHECK_THROWS_AS(FormatCatalog::load(t.path / "a.json"), std::runtime_error);
    t.write("b.json", R"([{"id":"x","nombre":"X","ancho":0,"alto":10}])");
    CHECK_THROWS_AS(FormatCatalog::load(t.path / "b.json"), std::runtime_error);
    t.write("c.json", R"([{"id":"x","nombre":"X","ancho":5,"alto":10},{"id":"x","nombre":"Y","ancho":5,"alto":10}])");
    CHECK_THROWS_WITH_AS(FormatCatalog::load(t.path / "c.json"), doctest::Contains("repetido"), std::runtime_error);
}

TEST_CASE("GET /api/formatos devuelve los tres formatos con sus dimensiones") {
    TempDir t("wc_fmt_http");
    t.write("web/index.html", "<html></html>");
    const FormatCatalog cat = FormatCatalog::load(fs::path(WCATALOG_SOURCE_DIR) / "templates" / "formatos.json");
    HttpServer server(t.path / "web");
    register_format_routes(server.raw(), cat);
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/api/formatos");
    REQUIRE(res);
    CHECK(res->status == 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    REQUIRE(body.size() == 3);
    CHECK(body[0]["id"] == "instagram-4x5");
    CHECK(body[0]["ancho"] == 1080);
    CHECK(body[0]["alto"] == 1350);
    CHECK(body[1]["ancho"] == 1920);
    CHECK(body[2]["alto"] == 1920);
    server.stop();
}

TEST_CASE("resolve_templates_dir prefiere templates/ junto al exe y cae al nivel superior") {
    TempDir t("wc_tpl_resolve");
    fs::create_directories(t.path / "build");
    fs::create_directories(t.path / "templates");
    CHECK(resolve_templates_dir(t.path / "build") == t.path / "templates");
    fs::create_directories(t.path / "build" / "templates");
    CHECK(resolve_templates_dir(t.path / "build") == t.path / "build" / "templates");
}
