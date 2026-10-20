#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <thread>

#include "db/migrations.hpp"
#include "http/json_response.hpp"
#include "http/routes_features.hpp"
#include "http/routes_houses.hpp"
#include "http/server.hpp"
#include "houses/feature_service.hpp"
#include "houses/house_service.hpp"
#include "settings/settings_service.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct Env {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<SettingsService> settings;
    std::unique_ptr<FeatureService> features;
    std::unique_ptr<HouseService> houses;

    explicit Env(const char* name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        db = open_app_database(dir / "c.db", dir / "backups");
        settings = std::make_unique<SettingsService>(*db);
        features = std::make_unique<FeatureService>(*db);
        houses = std::make_unique<HouseService>(*db, *settings, *features);
    }
    ~Env() {
        houses.reset();
        features.reset();
        settings.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::int64_t fid(const std::string& key) {
        for (const Feature& f : features->list()) {
            if (f.key == key) return f.id;
        }
        throw std::runtime_error("no existe " + key);
    }

    std::map<std::string, std::string> errors_of(const Json& payload) {
        try {
            houses->create(payload);
        } catch (const ValidationFailed& v) {
            return v.fields;
        }
        return {};
    }
};

std::vector<std::string> titles(const Json& list) {
    std::vector<std::string> out;
    for (const Json& h : list) out.push_back(h["titulo"]);
    return out;
}

}  // namespace

TEST_CASE("Casa: creacion solo con titulo -> disponible, consultar, sin precio, y aparece en la lista") {
    Env e("wcatalog-test-house1");
    Json h = e.houses->create(Json{{"titulo", "  Casa en Miramar  "}});
    CHECK(h["titulo"] == "Casa en Miramar");
    CHECK(h["estado"] == "disponible");
    CHECK(h["sin_precio"] == "consultar");
    CHECK(h["precio"].is_null());
    CHECK(h["moneda"].is_null());
    CHECK(h["operacion"].is_null());
    CHECK(h["lat"].is_null());
    CHECK(h["cuartos"].is_null());
    CHECK(h["caracteristicas"].empty());
    CHECK(h["badges"].empty());
    CHECK_FALSE(h["creado"].get<std::string>().empty());
    CHECK(titles(e.houses->list()) == std::vector<std::string>{"Casa en Miramar"});
}

TEST_CASE("Casa: titulo vacio o de espacios se rechaza") {
    Env e("wcatalog-test-house2");
    for (const Json& payload : {Json::object(), Json{{"titulo", ""}}, Json{{"titulo", "   "}}, Json{{"titulo", 5}}}) {
        auto f = e.errors_of(payload);
        REQUIRE(f.count("titulo") == 1);
        CHECK(f["titulo"].find("obligatorio") != std::string::npos);
    }
    CHECK(e.houses->list().empty());
}

TEST_CASE("Casa: datos fijos completos se guardan y se devuelven exactamente") {
    Env e("wcatalog-test-house3");
    Json in = {{"titulo", "Casa grande"}, {"descripcion", "Tres terrazas"}, {"operacion", "venta"}, {"estado", "reservada"},
               {"cuartos", 4}, {"banos", 2}, {"comedores_independientes", 2}, {"niveles", 2},
               {"area_construida_m2", 120}, {"area_terreno_m2", 300.5},
               {"direccion", "Calle 5 #12"}, {"barrio", "Vedado"}, {"municipio", "Plaza"}, {"provincia", "La Habana"}};
    Json h = e.houses->get(e.houses->create(in)["id"]);
    for (auto it = in.begin(); it != in.end(); ++it) CHECK_MESSAGE(h[it.key()] == it.value(), it.key());
}

TEST_CASE("Casa: valores negativos, decimales en enteros y textos largos se rechazan por campo") {
    Env e("wcatalog-test-house4");
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"cuartos", -1}}).count("cuartos") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"banos", 1.5}}).count("banos") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"niveles", "dos"}}).count("niveles") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"area_construida_m2", -5}}).count("area_construida_m2") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"area_terreno_m2", "grande"}}).count("area_terreno_m2") == 1);
    CHECK(e.errors_of(Json{{"titulo", std::string(201, 'x')}}).count("titulo") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"barrio", 7}}).count("barrio") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"operacion", "permuta"}}).count("operacion") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"estado", "rota"}}).count("estado") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"sin_precio", "quizas"}}).count("sin_precio") == 1);
    // varios errores a la vez
    auto f = e.errors_of(Json{{"titulo", ""}, {"cuartos", -1}, {"banos", -2}});
    CHECK(f.size() == 3);
    // blanco / null / numeros como texto
    Json ok = e.houses->create(Json{{"titulo", "A"}, {"cuartos", ""}, {"banos", nullptr}, {"niveles", "3"}});
    CHECK(ok["cuartos"].is_null());
    CHECK(ok["banos"].is_null());
    CHECK(ok["niveles"] == 3);
    CHECK(e.houses->list().size() == 1);  // las rechazadas no se guardaron
}

TEST_CASE("Casa: precio opcional, redondeo, moneda por defecto de los ajustes") {
    Env e("wcatalog-test-house5");
    Json none = e.houses->create(Json{{"titulo", "Sin precio"}, {"sin_precio", "ocultar"}});
    CHECK(none["precio"].is_null());
    CHECK(none["moneda"].is_null());
    CHECK(none["sin_precio"] == "ocultar");

    Json usd = e.houses->create(Json{{"titulo", "Con precio"}, {"precio", 85000.126}});
    CHECK(usd["precio"] == doctest::Approx(85000.13));
    CHECK(usd["moneda"] == "USD");  // por defecto

    e.settings->update(Json{{"general", {{"moneda", "EUR"}}}});
    Json eur = e.houses->create(Json{{"titulo", "Euro"}, {"precio", 1234.5}});
    CHECK(eur["moneda"] == "EUR");
    Json explicit_cur = e.houses->create(Json{{"titulo", "Peso"}, {"precio", 10}, {"moneda", "cup"}});
    CHECK(explicit_cur["moneda"] == "CUP");

    CHECK(e.errors_of(Json{{"titulo", "A"}, {"precio", -1}}).count("precio") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"precio", "caro"}}).count("precio") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"precio", 5}, {"moneda", "dolares"}}).count("moneda") == 1);
    // precio 0 es un precio valido
    CHECK(e.houses->create(Json{{"titulo", "Regalo"}, {"precio", 0}})["precio"] == 0);
}

TEST_CASE("Casa: coordenadas completas, en rango o ausentes") {
    Env e("wcatalog-test-house6");
    Json ok = e.houses->create(Json{{"titulo", "Con GPS"}, {"lat", 23.1136}, {"lon", -82.3666}});
    CHECK(ok["lat"] == doctest::Approx(23.1136));
    CHECK(ok["lon"] == doctest::Approx(-82.3666));
    CHECK(e.houses->create(Json{{"titulo", "Sin GPS"}})["lat"].is_null());

    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lat", 23.1}}).count("coordenadas") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lon", -82.3}}).count("coordenadas") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lat", 95}, {"lon", 0}}).count("coordenadas") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lat", 0}, {"lon", 181}}).count("coordenadas") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lat", "x"}, {"lon", "y"}}).count("coordenadas") == 1);
    CHECK(e.errors_of(Json{{"titulo", "A"}, {"lat", -90}, {"lon", 180}}).empty());  // extremos validos
}

TEST_CASE("Casa: busqueda sin tildes ni mayusculas, filtro por estado y orden por modificacion") {
    Env e("wcatalog-test-house7");
    auto a = e.houses->create(Json{{"titulo", "Casa colonial"}, {"barrio", "Vedado"}, {"municipio", "Plaza"}});
    auto b = e.houses->create(Json{{"titulo", "Apartamento"}, {"barrio", "Pe\xC3\xB1" "a Blanca"}, {"municipio", "Centro Habana"}, {"estado", "vendida"}});
    auto c = e.houses->create(Json{{"titulo", "Casa en el VEDADO"}, {"municipio", "Plaza"}, {"estado", "disponible"}});
    auto d = e.houses->create(Json{{"titulo", "Villa"}, {"municipio", "Playa"}, {"estado", "disponible"}});

    CHECK(titles(e.houses->list("vedado")) == std::vector<std::string>{"Casa en el VEDADO", "Casa colonial"});
    CHECK(titles(e.houses->list("V\xC3\x89" "DADO")) == std::vector<std::string>{"Casa en el VEDADO", "Casa colonial"});
    CHECK(titles(e.houses->list("pena")) == std::vector<std::string>{"Apartamento"});
    CHECK(titles(e.houses->list("Pe\xC3\xB1" "a")) == std::vector<std::string>{"Apartamento"});
    CHECK(titles(e.houses->list("plaza")).size() == 2);                       // municipio
    CHECK(titles(e.houses->list("casa vedado")).size() == 2);                  // palabras en AND
    CHECK(titles(e.houses->list("casa plaza")).size() == 2);
    CHECK(titles(e.houses->list("villa vedado")).empty());
    CHECK(titles(e.houses->list("%")).empty());                                // el comodin es literal
    CHECK(titles(e.houses->list("_")).empty());
    CHECK(e.houses->list("   ").size() == 4);

    CHECK(titles(e.houses->list("", "vendida")) == std::vector<std::string>{"Apartamento"});
    CHECK(titles(e.houses->list("", "disponible")).size() == 3);
    CHECK(titles(e.houses->list("vedado", "disponible")) == std::vector<std::string>{"Casa en el VEDADO", "Casa colonial"});
    CHECK_THROWS_AS(e.houses->list("", "rota"), ValidationFailed);

    // orden: la ultima modificada primero
    CHECK(titles(e.houses->list())[0] == "Villa");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    e.houses->update(a["id"], Json{{"titulo", "Casa colonial reformada"}, {"barrio", "Vedado"}});
    CHECK(titles(e.houses->list())[0] == "Casa colonial reformada");
    // renombrar actualiza el texto de busqueda
    CHECK(titles(e.houses->list("reformada")).size() == 1);
    CHECK(titles(e.houses->list("colonial", "")).size() == 1);
    (void)b; (void)c; (void)d;
}

TEST_CASE("Casa: estado vendida se refleja en la lista") {
    Env e("wcatalog-test-house8");
    Json h = e.houses->create(Json{{"titulo", "Casa"}});
    e.houses->update(h["id"], Json{{"titulo", "Casa"}, {"estado", "vendida"}});
    CHECK(e.houses->list()[0]["estado"] == "vendida");
}

TEST_CASE("Casa: valores de caracteristicas se guardan y se reabren tal cual, con badges") {
    Env e("wcatalog-test-house9");
    const auto gas = e.fid("gas_permanente"), net = e.fid("internet"), seg = e.fid("seguridad"), park = e.fid("parqueos");
    Json h = e.houses->create(Json{{"titulo", "Casa"},
                                   {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}},
                                                                    Json{{"caracteristica_id", net}, {"valor", "Fibra"}},
                                                                    Json{{"caracteristica_id", seg}, {"valor", Json::array({"C\xC3\xA1maras", "Rejas"})}},
                                                                    Json{{"caracteristica_id", park}, {"valor", 2}}})}});
    Json again = e.houses->get(h["id"]);
    REQUIRE(again["caracteristicas"].size() == 4);
    std::map<std::int64_t, Json> by_id;
    for (const Json& c : again["caracteristicas"]) by_id[c["caracteristica_id"]] = c["valor"];
    CHECK(by_id[gas] == true);
    CHECK(by_id[net] == "Fibra");
    CHECK(by_id[seg] == Json::array({"C\xC3\xA1maras", "Rejas"}));
    CHECK(by_id[park] == 2);

    std::vector<std::string> badge_texts;
    for (const Json& b : again["badges"]) badge_texts.push_back(b["texto"]);
    CHECK(badge_texts == std::vector<std::string>{"Gas permanente", "Internet: Fibra", "Seguridad: C\xC3\xA1maras, Rejas", "Parqueos: 2"});
    CHECK(again["badges"][0]["rol"] == "secundario");
    CHECK(again["badges"][3]["rol"] == "primario");
}

TEST_CASE("Casa: un valor de caracteristica invalido rechaza todo el guardado y no cambia nada") {
    Env e("wcatalog-test-house10");
    const auto gas = e.fid("gas_permanente"), net = e.fid("internet");
    Json h = e.houses->create(Json{{"titulo", "Original"},
                                   {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}}})}});
    const std::int64_t id = h["id"];

    // opcion inexistente en Internet
    try {
        e.houses->update(id, Json{{"titulo", "Cambiado"}, {"cuartos", 9},
                                  {"caracteristicas", Json::array({Json{{"caracteristica_id", net}, {"valor", "Satelite"}}})}});
        FAIL("debia rechazar");
    } catch (const ValidationFailed& v) {
        CHECK(v.fields.count("caracteristicas." + std::to_string(net)) == 1);
    }
    Json after = e.houses->get(id);
    CHECK(after["titulo"] == "Original");
    CHECK(after["cuartos"].is_null());
    CHECK(after["caracteristicas"].size() == 1);  // el valor anterior de Gas sigue ahi

    // alta con valor invalido: no se crea la casa
    CHECK_THROWS_AS(e.houses->create(Json{{"titulo", "Nueva"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", net}, {"valor", 5}}})}}),
                    ValidationFailed);
    CHECK(e.houses->list().size() == 1);

    // id desconocido y repetido
    auto f = e.errors_of(Json{{"titulo", "X"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", 99999}, {"valor", true}}})}});
    CHECK(f.count("caracteristicas.99999") == 1);
    auto g = e.errors_of(Json{{"titulo", "X"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}}, Json{{"caracteristica_id", gas}, {"valor", false}}})}});
    CHECK(g.count("caracteristicas." + std::to_string(gas)) == 1);
    CHECK(e.errors_of(Json{{"titulo", "X"}, {"caracteristicas", "no"}}).count("caracteristicas") == 1);
}

TEST_CASE("Casa: el PUT reemplaza los valores; los de caracteristicas inactivas se conservan") {
    Env e("wcatalog-test-house11");
    const auto gas = e.fid("gas_permanente"), pool = e.fid("piscina"), net = e.fid("internet");
    Json h = e.houses->create(Json{{"titulo", "Casa"},
                                   {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}},
                                                                    Json{{"caracteristica_id", pool}, {"valor", true}}})}});
    const std::int64_t id = h["id"];
    e.features->update(pool, Json{{"activa", false}});

    // el formulario ya no muestra Piscina: no la envia. Gas se quita, Internet se anade.
    Json r = e.houses->update(id, Json{{"titulo", "Casa"},
                                       {"caracteristicas", Json::array({Json{{"caracteristica_id", net}, {"valor", "ADSL"}}})}});
    std::set<std::int64_t> ids;
    for (const Json& c : r["caracteristicas"]) ids.insert(c["caracteristica_id"].get<std::int64_t>());
    CHECK(ids == std::set<std::int64_t>{pool, net});  // gas fuera; piscina (inactiva) conservada
    // y la inactiva no aparece en los badges
    std::vector<std::string> badge_texts;
    for (const Json& b : r["badges"]) badge_texts.push_back(b["texto"]);
    CHECK(badge_texts == std::vector<std::string>{"Internet: ADSL"});

    // valores vacios equivalen a "no informado"
    Json cleared = e.houses->update(id, Json{{"titulo", "Casa"},
                                             {"caracteristicas", Json::array({Json{{"caracteristica_id", net}, {"valor", ""}}})}});
    CHECK(cleared["caracteristicas"].size() == 1);  // solo la piscina inactiva
}

TEST_CASE("Casa: eliminar borra la casa y sus valores; ids inexistentes dan 404") {
    Env e("wcatalog-test-house12");
    const auto gas = e.fid("gas_permanente");
    Json h = e.houses->create(Json{{"titulo", "Casa"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}}})}});
    Json other = e.houses->create(Json{{"titulo", "Otra"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", gas}, {"valor", true}}})}});
    e.houses->remove(h["id"]);
    CHECK(titles(e.houses->list()) == std::vector<std::string>{"Otra"});
    Statement st = e.db->prepare("SELECT COUNT(*) FROM casa_caracteristica");
    st.step();
    CHECK(st.column_int(0) == 1);  // solo quedan los de la otra casa
    CHECK_THROWS_AS(e.houses->get(h["id"]), NotFoundError);
    CHECK_THROWS_AS(e.houses->remove(h["id"]), NotFoundError);
    CHECK_THROWS_AS(e.houses->update(99999, Json{{"titulo", "x"}}), NotFoundError);
    (void)other;
}

TEST_CASE("API casas: crear, buscar, filtrar, editar con errores por campo y eliminar") {
    Env e("wcatalog-test-houseapi");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_house_routes(server.raw(), *e.houses);
    register_feature_routes(server.raw(), *e.features);
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    auto post = [&](const std::string& p, const Json& b) { return cli.Post(p, b.dump(), "application/json"); };
    auto put = [&](const std::string& p, const Json& b) { return cli.Put(p, b.dump(), "application/json"); };

    // creacion minima
    auto min = post("/api/casas", Json{{"titulo", "Casa en Miramar"}});
    REQUIRE(min);
    CHECK(min->status == 201);
    Json created = Json::parse(min->body);
    CHECK(created["estado"] == "disponible");
    const std::string id = std::to_string(created["id"].get<std::int64_t>());

    // errores por campo
    auto bad = post("/api/casas", Json{{"titulo", ""}, {"cuartos", -1}, {"lat", 95}, {"lon", 0}});
    REQUIRE(bad);
    CHECK(bad->status == 400);
    Json fields = Json::parse(bad->body)["error"]["fields"];
    CHECK(fields.contains("titulo"));
    CHECK(fields.contains("cuartos"));
    CHECK(fields.contains("coordenadas"));
    CHECK(post("/api/casas", Json::array())->status == 400);
    CHECK(cli.Post("/api/casas", "no json", "application/json")->status == 400);

    // mas casas y busqueda
    post("/api/casas", Json{{"titulo", "Casa colonial"}, {"barrio", "Vedado"}, {"estado", "vendida"}});
    post("/api/casas", Json{{"titulo", "Apto"}, {"barrio", "Pe\xC3\xB1" "a"}});

    auto q1 = cli.Get("/api/casas?q=vedado");
    REQUIRE(q1);
    CHECK(Json::parse(q1->body).size() == 1);
    auto q2 = cli.Get("/api/casas?q=pena");
    REQUIRE(q2);
    Json r2 = Json::parse(q2->body);
    REQUIRE(r2.size() == 1);
    CHECK(r2[0]["titulo"] == "Apto");
    CHECK(r2[0].contains("cuartos"));
    CHECK_FALSE(r2[0].contains("caracteristicas"));  // la lista es solo resumen
    auto q3 = cli.Get("/api/casas?estado=vendida");
    CHECK(Json::parse(q3->body).size() == 1);
    CHECK(cli.Get("/api/casas?estado=rota")->status == 400);
    CHECK(Json::parse(cli.Get("/api/casas")->body).size() == 3);

    // detalle con caracteristicas y badges
    Json gas = Json::parse(cli.Get("/api/caracteristicas")->body)[0];
    auto upd = put("/api/casas/" + id, Json{{"titulo", "Casa en Miramar"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", gas["id"]}, {"valor", true}}})}});
    REQUIRE(upd);
    CHECK(upd->status == 200);
    auto detail = cli.Get("/api/casas/" + id);
    REQUIRE(detail);
    Json d = Json::parse(detail->body);
    CHECK(d["caracteristicas"].size() == 1);
    CHECK(d["badges"][0]["texto"] == "Gas permanente");
    CHECK(d["badges"][0]["rol"] == "secundario");

    // PUT invalido: 400 con fields y nada cambia
    auto badput = put("/api/casas/" + id, Json{{"titulo", "Roto"}, {"caracteristicas", Json::array({Json{{"caracteristica_id", gas["id"]}, {"valor", "tal vez"}}})}});
    REQUIRE(badput);
    CHECK(badput->status == 400);
    CHECK(Json::parse(badput->body)["error"]["fields"].contains("caracteristicas." + std::to_string(gas["id"].get<std::int64_t>())));
    CHECK(Json::parse(cli.Get("/api/casas/" + id)->body)["titulo"] == "Casa en Miramar");
    CHECK(put("/api/casas/99999", Json{{"titulo", "x"}})->status == 404);
    CHECK(cli.Get("/api/casas/99999")->status == 404);

    // vista previa de badges sin guardar
    auto prev = post("/api/casas/previsualizar-badges",
                     Json{{"caracteristicas", Json::array({Json{{"caracteristica_id", gas["id"]}, {"valor", true}}})}});
    REQUIRE(prev);
    CHECK(prev->status == 200);
    CHECK(Json::parse(prev->body)["badges"][0]["texto"] == "Gas permanente");

    // eliminar
    auto del = cli.Delete("/api/casas/" + id);
    REQUIRE(del);
    CHECK(del->status == 200);
    CHECK(cli.Get("/api/casas/" + id)->status == 404);
    CHECK(cli.Delete("/api/casas/" + id)->status == 404);
    CHECK(Json::parse(cli.Get("/api/casas")->body).size() == 2);
    server.stop();
}
