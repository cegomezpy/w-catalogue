#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "db/migrations.hpp"
#include "fonts/font_library.hpp"
#include "fonts/font_metadata.hpp"
#include "http/json_response.hpp"
#include "http/routes_fonts.hpp"
#include "http/routes_settings.hpp"
#include "http/server.hpp"
#include "settings/settings_service.hpp"

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

// Copia de una fuente con el nombre de familia sustituido (misma longitud) para simular "Lobos"
std::string rename_family(std::string bytes, const std::string& from, const std::string& to) {
    // nameID en UTF-16BE: cada carácter ocupa 2 bytes con un 0 delante
    auto wide = [](const std::string& s) {
        std::string w;
        for (char c : s) { w += '\0'; w += c; }
        return w;
    };
    std::string a = wide(from), b = wide(to);
    for (size_t pos = bytes.find(a); pos != std::string::npos; pos = bytes.find(a, pos + b.size())) {
        bytes.replace(pos, a.size(), b);
    }
    for (size_t pos = bytes.find(from); pos != std::string::npos; pos = bytes.find(from, pos + to.size())) {
        bytes.replace(pos, from.size(), to);  // nombres Macintosh
    }
    return bytes;
}

}  // namespace

TEST_CASE("Biblioteca: el manifiesto lista las 4 familias incluidas con pesos 400 y 700") {
    Env e("wcatalog-test-font1");
    Json fam = e.lib->families_json();
    std::set<std::string> names;
    for (const Json& f : fam) {
        names.insert(f["familia"]);
        CHECK(f["origen"] == "incluida");
        CHECK(f["variantes"].size() == 2);
        for (const Json& v : f["variantes"]) CHECK(v["id"].is_null());
    }
    CHECK(names == std::set<std::string>{"Inter", "Montserrat", "Playfair Display", "Poppins"});
    CHECK(e.lib->family_exists("Inter"));
    CHECK_FALSE(e.lib->family_exists("Lobos"));
}

TEST_CASE("Biblioteca: faltando el manifiesto se informa") {
    Env e("wcatalog-test-font1b");
    FontLibrary broken(*e.db, *e.settings, FontPaths{e.dir / "no-existe", e.dir / "i", e.dir / "t"});
    CHECK_THROWS_AS(broken.load_manifest(), std::runtime_error);
}

TEST_CASE("Metadatos: familia, peso y estilo de las TTF incluidas") {
    FontMetadata r = read_font_metadata(slurp(kFontsDir / "Inter-Regular.ttf"));
    CHECK(r.family == "Inter");
    CHECK(r.weight == 400);
    CHECK_FALSE(r.italic);
    CHECK(r.format == "ttf");

    FontMetadata b = read_font_metadata(slurp(kFontsDir / "PlayfairDisplay-Bold.ttf"));
    CHECK(b.family == "Playfair Display");
    CHECK(b.weight == 700);

    // Poppins no trae nameID 16: se usa el 1
    FontMetadata p = read_font_metadata(slurp(kFontsDir / "Poppins-Bold.ttf"));
    CHECK(p.family == "Poppins");
    CHECK(p.weight == 700);
}

TEST_CASE("Metadatos: archivos no validos se rechazan con mensaje") {
    CHECK_THROWS_AS(read_font_metadata(""), FontError);
    CHECK_THROWS_AS(read_font_metadata("esto no es una fuente, es texto plano"), FontError);
    std::string good = slurp(kFontsDir / "Inter-Regular.ttf");
    CHECK_THROWS_AS(read_font_metadata(good.substr(0, 200)), FontError);  // truncada
    std::string big = good;
    big.resize(kMaxFontBytes + 1, '\0');
    try {
        read_font_metadata(big);
        FAIL("debia rechazar por tamano");
    } catch (const FontError& e) {
        CHECK(std::string(e.what()).find("10 MB") != std::string::npos);
    }
}

TEST_CASE("Importar: flujo analizar -> importar, variantes de una familia y duplicado") {
    Env e("wcatalog-test-font2");
    std::string regular = rename_family(slurp(kFontsDir / "Inter-Regular.ttf"), "Inter", "Lobos");
    std::string bold = rename_family(slurp(kFontsDir / "Inter-Bold.ttf"), "Inter", "Lobos");

    Json a = e.lib->analyze(regular, "Lobos-Regular.ttf");
    CHECK(a["familia"] == "Lobos");
    CHECK(a["peso"] == 400);
    CHECK(a["estilo"] == "normal");
    REQUIRE(fs::exists(e.dir / "data" / "tmp" / (a["token"].get<std::string>() + ".ttf")));

    Json v1 = e.lib->import_font(Json{{"token", a["token"]}, {"familia", "Lobos"}, {"peso", 400}, {"estilo", "normal"}});
    CHECK(v1["origen"] == "importada");
    CHECK(fs::exists(e.dir / "data" / "fuentes" / v1["archivo"].get<std::string>()));
    CHECK_FALSE(fs::exists(e.dir / "data" / "tmp" / (a["token"].get<std::string>() + ".ttf")));  // el temporal se consume

    Json b = e.lib->analyze(bold, "Lobos-Bold.ttf");
    e.lib->import_font(Json{{"token", b["token"]}, {"familia", "Lobos"}, {"peso", 700}, {"estilo", "normal"}});

    Json fam = e.lib->families_json();
    int lobos = 0;
    for (const Json& f : fam) {
        if (f["familia"] == "Lobos") {
            ++lobos;
            CHECK(f["variantes"].size() == 2);
            CHECK(f["origen"] == "importada");
        }
    }
    CHECK(lobos == 1);
    CHECK(e.lib->family_exists("Lobos"));

    // duplicado sin reemplazar: 409 y no se toca nada
    Json dup = e.lib->analyze(regular, "otra-Lobos-Regular.ttf");
    try {
        e.lib->import_font(Json{{"token", dup["token"]}, {"familia", "Lobos"}, {"peso", 400}, {"estilo", "normal"}});
        FAIL("debia detectar el duplicado");
    } catch (const FontError& err) {
        CHECK(err.kind == FontError::Kind::Conflict);
    }
    // con reemplazo: sigue habiendo una sola variante 400
    Json again = e.lib->import_font(
        Json{{"token", dup["token"]}, {"familia", "Lobos"}, {"peso", 400}, {"estilo", "normal"}, {"reemplazar", true}});
    CHECK(again["id"] == v1["id"]);
    CHECK(e.lib->family_variants("Lobos").size() == 2);
}

TEST_CASE("Importar: validaciones de metadatos y de token") {
    Env e("wcatalog-test-font3");
    Json a = e.lib->analyze(slurp(kFontsDir / "Inter-Regular.ttf"), "x.ttf");
    auto bad = [&](Json patch) {
        Json req = Json{{"token", a["token"]}, {"familia", "Zeta"}, {"peso", 400}, {"estilo", "normal"}};
        req.update(patch);
        CHECK_THROWS_AS(e.lib->import_font(req), FontError);
    };
    bad(Json{{"familia", ""}});
    bad(Json{{"familia", "Mala\"; } body { display:none"}});
    bad(Json{{"peso", 450}});
    bad(Json{{"peso", "700"}});
    bad(Json{{"estilo", "oblique"}});
    bad(Json{{"token", "../../etc/passwd"}});
    bad(Json{{"token", "00000000000000000000000000000000"}});
    // la variante incluida no se puede pisar
    bad(Json{{"familia", "Inter"}});
}

TEST_CASE("Eliminar: borra el archivo y restablece el ajuste en uso con avisos") {
    Env e("wcatalog-test-font4");
    Json a = e.lib->analyze(rename_family(slurp(kFontsDir / "Inter-Regular.ttf"), "Inter", "Lobos"), "Lobos-Regular.ttf");
    Json v = e.lib->import_font(Json{{"token", a["token"]}, {"familia", "Lobos"}, {"peso", 400}, {"estilo", "normal"}});
    fs::path file = e.dir / "data" / "fuentes" / v["archivo"].get<std::string>();
    REQUIRE(fs::exists(file));

    e.settings->update(Json{{"tipografia", {{"fuenteTitulos", "Lobos"}}}});
    CHECK(e.settings->value(SettingsService::kFuenteTitulos) == "Lobos");

    Json r = e.lib->remove(v["id"].get<std::int64_t>());
    CHECK_FALSE(fs::exists(file));
    CHECK_FALSE(e.lib->family_exists("Lobos"));
    CHECK(e.settings->value(SettingsService::kFuenteTitulos) == "Playfair Display");
    REQUIRE(r["avisos"].size() == 1);
    CHECK(r["avisos"][0].get<std::string>().find("Lobos") != std::string::npos);

    CHECK_THROWS_AS(e.lib->remove(9999), FontError);
}

TEST_CASE("Eliminar: una fuente que no estaba en uso no genera avisos") {
    Env e("wcatalog-test-font5");
    Json a = e.lib->analyze(rename_family(slurp(kFontsDir / "Inter-Regular.ttf"), "Inter", "Lobos"), "x.ttf");
    Json v = e.lib->import_font(Json{{"token", a["token"]}, {"familia", "Lobos"}, {"peso", 400}, {"estilo", "normal"}});
    Json r = e.lib->remove(v["id"].get<std::int64_t>());
    CHECK(r["avisos"].empty());
}

TEST_CASE("cleanup_tmp borra solo temporales viejos") {
    Env e("wcatalog-test-font6");
    fs::create_directories(e.dir / "data" / "tmp");
    fs::path old_f = e.dir / "data" / "tmp" / "viejo.ttf", new_f = e.dir / "data" / "tmp" / "nuevo.ttf";
    std::ofstream(old_f) << "x";
    std::ofstream(new_f) << "x";
    fs::last_write_time(old_f, fs::file_time_type::clock::now() - std::chrono::hours(3));
    e.lib->cleanup_tmp(1);
    CHECK_FALSE(fs::exists(old_f));
    CHECK(fs::exists(new_f));
}

TEST_CASE("API fuentes: importar, listar, servir y eliminar") {
    Env e("wcatalog-test-font7");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_font_routes(server.raw(), *e.lib);
    register_settings_routes(server.raw(), *e.settings);
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto list = cli.Get("/api/fuentes");
    REQUIRE(list);
    CHECK(Json::parse(list->body).size() == 4);

    // fuente incluida servida con tipo font/ttf y contenido idéntico
    auto inc = cli.Get("/media/fuentes/incluidas/Inter-Regular.ttf");
    REQUIRE(inc);
    CHECK(inc->status == 200);
    CHECK(inc->get_header_value("Content-Type") == "font/ttf");
    CHECK(inc->body == slurp(kFontsDir / "Inter-Regular.ttf"));

    // rutas no permitidas
    auto lic = cli.Get("/media/fuentes/incluidas/fuentes.json");
    REQUIRE(lic);
    CHECK(lic->status == 404);
    auto trav = cli.Get("/media/fuentes/incluidas/..%2Ffuentes.json");
    REQUIRE(trav);
    CHECK(trav->status == 404);

    // analizar sin archivo
    auto nofile = cli.Post("/api/fuentes/analizar", httplib::UploadFormDataItems{{"otro", "x", "", ""}});
    REQUIRE(nofile);
    CHECK(nofile->status == 400);

    // archivo inválido
    auto junk = cli.Post("/api/fuentes/analizar",
                         httplib::UploadFormDataItems{{"archivo", "no es una fuente", "x.ttf", "font/ttf"}});
    REQUIRE(junk);
    CHECK(junk->status == 400);
    CHECK(Json::parse(junk->body)["error"]["message"].get<std::string>().find("TrueType") != std::string::npos);
    CHECK((!fs::exists(e.dir / "data" / "fuentes") || fs::is_empty(e.dir / "data" / "fuentes")));

    // flujo correcto
    std::string lobos = rename_family(slurp(kFontsDir / "Inter-Bold.ttf"), "Inter", "Lobos");
    auto an = cli.Post("/api/fuentes/analizar", httplib::UploadFormDataItems{{"archivo", lobos, "Lobos-Bold.ttf", "font/ttf"}});
    REQUIRE(an);
    REQUIRE(an->status == 200);
    Json meta = Json::parse(an->body);
    CHECK(meta["familia"] == "Lobos");
    CHECK(meta["peso"] == 700);

    Json imp = {{"token", meta["token"]}, {"familia", "Lobos"}, {"peso", 700}, {"estilo", "normal"}};
    auto ok = cli.Post("/api/fuentes", imp.dump(), "application/json");
    REQUIRE(ok);
    CHECK(ok->status == 201);
    Json created = Json::parse(ok->body);

    auto served = cli.Get(created["url"].get<std::string>());
    REQUIRE(served);
    CHECK(served->status == 200);
    CHECK(served->body == lobos);

    // duplicado -> 409
    auto an2 = cli.Post("/api/fuentes/analizar", httplib::UploadFormDataItems{{"archivo", lobos, "Lobos-Bold.ttf", "font/ttf"}});
    Json imp2 = {{"token", Json::parse(an2->body)["token"]}, {"familia", "Lobos"}, {"peso", 700}, {"estilo", "normal"}};
    auto dup = cli.Post("/api/fuentes", imp2.dump(), "application/json");
    REQUIRE(dup);
    CHECK(dup->status == 409);
    imp2["reemplazar"] = true;
    auto rep = cli.Post("/api/fuentes", imp2.dump(), "application/json");
    REQUIRE(rep);
    CHECK(rep->status == 201);

    // usar la fuente en ajustes y eliminarla
    auto put = cli.Put("/api/ajustes", Json{{"tipografia", {{"fuenteTitulos", "Lobos"}}}}.dump(), "application/json");
    REQUIRE(put);
    CHECK(put->status == 200);

    auto del = cli.Delete("/api/fuentes/" + std::to_string(created["id"].get<std::int64_t>()));
    REQUIRE(del);
    CHECK(del->status == 200);
    CHECK(Json::parse(del->body)["avisos"].size() == 1);
    auto after = cli.Get("/api/ajustes");
    CHECK(Json::parse(after->body)["tipografia"]["fuenteTitulos"] == "Playfair Display");

    auto gone = cli.Delete("/api/fuentes/" + std::to_string(created["id"].get<std::int64_t>()));
    REQUIRE(gone);
    CHECK(gone->status == 404);
    server.stop();
}

TEST_CASE("Metadatos: fuentes truncadas o corrompidas nunca bloquean la aplicacion") {
    // stb_truetype no comprueba limites; la validacion propia debe rechazar (o aceptar) sin leer fuera
    for (const char* file : {"Inter-Regular.ttf", "Poppins-Bold.ttf"}) {
        const std::string good = slurp(kFontsDir / file);
        REQUIRE_NOTHROW(read_font_metadata(good));

        auto outcome = [&](const std::string& data) {
            try {
                read_font_metadata(data);
            } catch (const FontError&) {
            }
            return true;  // lo importante: no se cuelga ni se sale del buffer
        };
        // truncada en muchos puntos (cabecera, directorio de tablas, cuerpo)
        for (size_t len = 0; len < 600; len += 7) CHECK(outcome(good.substr(0, len)));
        for (size_t len = 600; len < good.size(); len += good.size() / 60 + 1) CHECK(outcome(good.substr(0, len)));
        // bytes alterados al azar (generador fijo para que sea reproducible)
        std::uint32_t seed = 12345;
        auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (int round = 0; round < 400; ++round) {
            std::string bad = good;
            const int flips = 1 + static_cast<int>(next() % 6);
            for (int k = 0; k < flips; ++k) {
                // concentrado al inicio, donde estan el directorio de tablas y las tablas pequenas
                size_t pos = next() % (round % 2 ? 400 : bad.size());
                bad[pos] = static_cast<char>(next() & 0xFF);
            }
            CHECK(outcome(bad));
        }
    }
    // un directorio de tablas que apunta fuera del archivo se rechaza
    std::string good = slurp(kFontsDir / "Inter-Regular.ttf");
    std::string bad = good;
    bad[12 + 8] = static_cast<char>(0x7F);  // offset de la primera tabla enorme
    CHECK_THROWS_AS(read_font_metadata(bad), FontError);
}
