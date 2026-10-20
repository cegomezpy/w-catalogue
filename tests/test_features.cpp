#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>

#include "db/migrations.hpp"
#include "http/guarded.hpp"
#include "http/json_response.hpp"
#include "http/routes_features.hpp"
#include "http/server.hpp"
#include "houses/badges.hpp"
#include "houses/feature_service.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct Env {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<FeatureService> svc;

    explicit Env(const char* name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        db = open_app_database(dir / "c.db", dir / "backups");
        svc = std::make_unique<FeatureService>(*db);
    }
    ~Env() {
        svc.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::int64_t add_house(const std::string& title = "Casa") {
        Statement st = db->prepare(
            "INSERT INTO casa (titulo, creado, actualizado) VALUES (?, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')");
        st.bind(1, title);
        st.step();
        return db->last_insert_rowid();
    }
    void set_value(std::int64_t house, std::int64_t feature, const std::string& v) {
        Statement st = db->prepare("INSERT OR REPLACE INTO casa_caracteristica (casa_id, caracteristica_id, valor) VALUES (?, ?, ?)");
        st.bind(1, house).bind(2, feature).bind(3, v);
        st.step();
    }
    Feature by_key(const std::string& key) {
        for (const Feature& f : svc->list()) {
            if (f.key == key) return f;
        }
        throw std::runtime_error("no existe " + key);
    }
};

Json feature_payload(const std::string& label, const std::string& type, Json extra = Json::object()) {
    Json p = {{"etiqueta", label}, {"categoria", "espacios"}, {"tipo", type}};
    p.update(extra);
    return p;
}

}  // namespace

TEST_CASE("Caracteristicas: crear con clave generada, orden al final y valores por defecto") {
    Env e("wcatalog-test-feat1");
    Feature f = e.svc->create(feature_payload("Vista al mar", "bool"));
    CHECK(f.key == "vista_al_mar");
    CHECK(f.order == 17);
    CHECK(f.active);
    CHECK_FALSE(f.predefined);
    CHECK(f.badge_visible);
    CHECK(f.badge_role == "secundario");
    CHECK_FALSE(f.badge_text.has_value());

    Feature g = e.svc->create(feature_payload("Vista al mar", "bool"));  // misma etiqueta: clave con sufijo
    CHECK(g.key == "vista_al_mar_2");
    Feature h = e.svc->create(feature_payload("\xC3\x81rea de juegos \xC3\xB1" "and\xC3\xBA", "texto"));
    CHECK(h.key == "area_de_juegos_nandu");
    CHECK(e.svc->list().size() == 19);
}

TEST_CASE("Caracteristicas: validacion de campos al crear") {
    Env e("wcatalog-test-feat2");
    auto fields_of = [&](const Json& payload) {
        try {
            e.svc->create(payload);
        } catch (const ValidationFailed& v) {
            return v.fields;
        }
        return std::map<std::string, std::string>{};
    };
    CHECK(fields_of(Json::object()).size() == 3);  // etiqueta, categoria, tipo
    CHECK(fields_of(feature_payload("", "bool")).count("etiqueta") == 1);
    CHECK(fields_of(feature_payload("X", "raro")).count("tipo") == 1);
    CHECK(fields_of(Json{{"etiqueta", "X"}, {"categoria", "jardin"}, {"tipo", "bool"}}).count("categoria") == 1);
    // opcion sin opciones
    CHECK(fields_of(feature_payload("Orientacion", "opcion")).count("opciones") == 1);
    CHECK(fields_of(feature_payload("Orientacion", "multiopcion", Json{{"opciones", Json::array({" ", ""})}})).count("opciones") == 1);
    CHECK(fields_of(feature_payload("X", "bool", Json{{"badge", {{"rol", "rojo"}}}})).count("badge.rol") == 1);
    CHECK(fields_of(feature_payload("X", "bool", Json{{"icono", "Mi Icono"}})).count("icono") == 1);
    // todo correcto
    CHECK(fields_of(feature_payload("Orientacion", "opcion", Json{{"opciones", Json::array({"Norte", "Sur", "Norte"})}})).empty());
    CHECK(e.by_key("orientacion").options == std::vector<std::string>{"Norte", "Sur"});
}

TEST_CASE("Caracteristicas: tipos sin opciones descartan la lista") {
    Env e("wcatalog-test-feat3");
    Feature f = e.svc->create(feature_payload("Mascotas", "bool", Json{{"opciones", Json::array({"a"})}}));
    CHECK(f.options.empty());
}

TEST_CASE("Caracteristicas: editar etiqueta, badge y estado; predefinidas se pueden editar y desactivar") {
    Env e("wcatalog-test-feat4");
    Feature gas = e.by_key("gas_permanente");
    Feature up = e.svc->update(gas.id, Json{{"etiqueta", "Gas de ciudad"},
                                            {"badge", {{"texto", "Con {valor}"}, {"rol", "primario"}, {"visible", true}}},
                                            {"activa", false}});
    CHECK(up.label == "Gas de ciudad");
    CHECK(up.key == "gas_permanente");  // la clave no cambia
    CHECK(up.predefined);
    CHECK_FALSE(up.active);
    CHECK(*up.badge_text == "Con {valor}");
    CHECK(up.badge_role == "primario");
    CHECK(up.category == gas.category);  // lo no enviado se conserva
    // texto vacio vuelve al texto por defecto
    Feature reset = e.svc->update(gas.id, Json{{"badge", {{"texto", ""}}}});
    CHECK_FALSE(reset.badge_text.has_value());
    CHECK_THROWS_AS(e.svc->update(9999, Json{{"etiqueta", "x"}}), NotFoundError);
    CHECK_THROWS_AS(e.svc->update(gas.id, Json{{"etiqueta", ""}}), ValidationFailed);
}

TEST_CASE("Caracteristicas: no se elimina una predefinida pero si una propia (con sus valores en cascada)") {
    Env e("wcatalog-test-feat5");
    Feature gas = e.by_key("gas_permanente");
    try {
        e.svc->remove(gas.id);
        FAIL("debia rechazar");
    } catch (const ConflictError& c) {
        CHECK(std::string(c.what()).find("desactivarla") != std::string::npos);
    }
    CHECK(e.svc->get(gas.id).id == gas.id);

    Feature mine = e.svc->create(feature_payload("Vista al mar", "bool"));
    for (int i = 0; i < 3; ++i) e.set_value(e.add_house(), mine.id, "1");
    CHECK(e.svc->usage_count(mine.id) == 3);
    e.svc->remove(mine.id);
    CHECK_THROWS_AS(e.svc->get(mine.id), NotFoundError);
    Statement st = e.db->prepare("SELECT COUNT(*) FROM casa_caracteristica WHERE caracteristica_id = ?");
    st.bind(1, mine.id);
    st.step();
    CHECK(st.column_int(0) == 0);
}

TEST_CASE("Caracteristicas: no se cambia el tipo si hay valores; si no los hay, si") {
    Env e("wcatalog-test-feat6");
    Feature f = e.svc->create(feature_payload("Orientacion", "opcion", Json{{"opciones", Json::array({"Norte", "Sur"})}}));
    CHECK_NOTHROW(e.svc->update(f.id, Json{{"tipo", "texto"}}));  // sin valores
    CHECK(e.svc->get(f.id).options.empty());

    Feature g = e.svc->create(feature_payload("Terraza techada", "bool"));
    e.set_value(e.add_house(), g.id, "1");
    e.set_value(e.add_house(), g.id, "0");
    try {
        e.svc->update(g.id, Json{{"tipo", "texto"}});
        FAIL("debia rechazar");
    } catch (const ConflictError& c) {
        CHECK(std::string(c.what()).find("2 casas") != std::string::npos);
    }
    CHECK(e.svc->get(g.id).type == "bool");
    CHECK_NOTHROW(e.svc->update(g.id, Json{{"tipo", "bool"}, {"etiqueta", "Terraza cubierta"}}));  // mismo tipo
}

TEST_CASE("Caracteristicas: no se quita una opcion en uso (unica ni multiple)") {
    Env e("wcatalog-test-feat7");
    Feature internet = e.by_key("internet");
    e.set_value(e.add_house(), internet.id, "Fibra");
    e.set_value(e.add_house(), internet.id, "Fibra");

    try {
        e.svc->update(internet.id, Json{{"opciones", Json::array({"ADSL", "Datos m\xC3\xB3viles", "Otro"})}});
        FAIL("debia rechazar");
    } catch (const ConflictError& c) {
        std::string m = c.what();
        CHECK(m.find("Fibra") != std::string::npos);
        CHECK(m.find("2 casas") != std::string::npos);
    }
    CHECK(e.svc->get(internet.id).options.size() == 4);
    // quitar una opcion sin uso y anadir otra si se puede
    CHECK_NOTHROW(e.svc->update(internet.id, Json{{"opciones", Json::array({"Fibra", "ADSL", "Datos m\xC3\xB3viles", "Satelite"})}}));

    Feature seg = e.by_key("seguridad");
    e.set_value(e.add_house(), seg.id, "[\"Rejas\",\"C\xC3\xA1maras\"]");
    CHECK_THROWS_AS(e.svc->update(seg.id, Json{{"opciones", Json::array({"Rejas", "Alarma"})}}), ConflictError);  // quita Camaras
    CHECK_NOTHROW(e.svc->update(seg.id, Json{{"opciones", Json::array({"Rejas", "C\xC3\xA1maras", "Perro"})}}));
    // renombrar = quitar + anadir: mismo bloqueo
    CHECK_THROWS_AS(e.svc->update(seg.id, Json{{"opciones", Json::array({"Rejas", "Camaras", "Perro"})}}), ConflictError);
}

TEST_CASE("Caracteristicas: reordenar") {
    Env e("wcatalog-test-feat8");
    auto all = e.svc->list();
    std::vector<std::int64_t> ids = {all[2].id, all[0].id};
    e.svc->reorder(ids);
    auto after = e.svc->list();
    CHECK(after[0].id == all[2].id);
    CHECK(after[1].id == all[0].id);
    CHECK(after[2].id == all[1].id);  // el resto sigue detras en su orden
    CHECK(after.size() == all.size());
    CHECK_THROWS_AS(e.svc->reorder({all[0].id, all[0].id}), ValidationFailed);
    CHECK_THROWS_AS(e.svc->reorder({99999}), ValidationFailed);
}

// ---- badges (D6) ----

namespace {

std::vector<std::string> badge_texts(const std::vector<Badge>& b) {
    std::vector<std::string> out;
    for (const auto& x : b) out.push_back(x.text);
    return out;
}

}  // namespace

TEST_CASE("Badges: escenario del spec (gas, fibra, respaldo energetico)") {
    Env e("wcatalog-test-badge1");
    auto cat = e.svc->list();
    std::map<std::int64_t, std::string> v;
    v[e.by_key("gas_permanente").id] = "1";
    v[e.by_key("internet").id] = "Fibra";
    v[e.by_key("respaldo_energetico").id] = "[\"Planta el\xC3\xA9" "ctrica\",\"Paneles solares\"]";
    auto badges = resolve_badges(cat, v);
    REQUIRE(badges.size() == 3);
    CHECK(badge_texts(badges) == std::vector<std::string>{"Gas permanente", "Respaldo energ\xC3\xA9tico: Planta el\xC3\xA9" "ctrica, Paneles solares", "Internet: Fibra"});
    for (const auto& b : badges) CHECK(b.role == "secundario");
    CHECK(badges[0].icon == "flame");
}

TEST_CASE("Badges: valor no en si/no, valor ausente y vacio no aparecen") {
    Env e("wcatalog-test-badge2");
    auto cat = e.svc->list();
    std::map<std::int64_t, std::string> v;
    v[e.by_key("piscina").id] = "0";
    v[e.by_key("terraza").id] = "1";
    v[e.by_key("internet").id] = "";
    auto b = resolve_badges(cat, v);
    CHECK(badge_texts(b) == std::vector<std::string>{"Terraza"});
}

TEST_CASE("Badges: plantilla {valor}, rol personalizado y numero con unidad") {
    Env e("wcatalog-test-badge3");
    Feature internet = e.by_key("internet");
    e.svc->update(internet.id, Json{{"badge", {{"texto", "Internet {valor}"}, {"rol", "secundario"}}}});
    Feature park = e.by_key("parqueos");
    e.svc->update(park.id, Json{{"unidad", "plazas"}, {"badge", {{"rol", "terciario"}}}});
    Feature fixed = e.svc->create(feature_payload("Con vista", "bool", Json{{"badge", {{"texto", "Vista incluida"}}}}));
    Feature twice = e.svc->create(feature_payload("Zona", "texto", Json{{"badge", {{"texto", "{valor} ({valor})"}}}}));

    std::map<std::int64_t, std::string> v = {{internet.id, "Fibra"}, {park.id, "2"}, {fixed.id, "1"}, {twice.id, "Centro"}};
    auto b = resolve_badges(e.svc->list(), v);
    CHECK(badge_texts(b) == std::vector<std::string>{"Internet Fibra", "Parqueos: 2 plazas", "Vista incluida", "Centro (Centro)"});
    CHECK(b[1].role == "terciario");
}

TEST_CASE("Badges: badge oculto y caracteristica inactiva no aparecen aunque tengan valor") {
    Env e("wcatalog-test-badge4");
    Feature anio = e.by_key("anio_construccion");  // badge oculto de fabrica
    Feature piscina = e.by_key("piscina");
    Feature gas = e.by_key("gas_permanente");
    std::map<std::int64_t, std::string> v = {{anio.id, "1995"}, {piscina.id, "1"}, {gas.id, "1"}};
    CHECK(badge_texts(resolve_badges(e.svc->list(), v)) == std::vector<std::string>{"Gas permanente", "Piscina"});

    e.svc->update(piscina.id, Json{{"activa", false}});
    CHECK(badge_texts(resolve_badges(e.svc->list(), v)) == std::vector<std::string>{"Gas permanente"});

    e.svc->update(anio.id, Json{{"badge", {{"visible", true}}}});
    CHECK(badge_texts(resolve_badges(e.svc->list(), v)) ==
          std::vector<std::string>{"Gas permanente", "A\xC3\xB1o de construcci\xC3\xB3n: 1995"});
}

TEST_CASE("Badges: siguen el orden del catalogo") {
    Env e("wcatalog-test-badge5");
    Feature gas = e.by_key("gas_permanente"), terraza = e.by_key("terraza");
    std::map<std::int64_t, std::string> v = {{gas.id, "1"}, {terraza.id, "1"}};
    e.svc->reorder({terraza.id});
    CHECK(badge_texts(resolve_badges(e.svc->list(), v)) == std::vector<std::string>{"Terraza", "Gas permanente"});
}

// ---- API ----

TEST_CASE("API caracteristicas: alta, edicion, orden y eliminacion en uso") {
    Env e("wcatalog-test-featapi");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_feature_routes(server.raw(), *e.svc);
    int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    auto json_post = [&](const std::string& path, const Json& b) { return cli.Post(path, b.dump(), "application/json"); };
    auto json_put = [&](const std::string& path, const Json& b) { return cli.Put(path, b.dump(), "application/json"); };

    auto list = cli.Get("/api/caracteristicas");
    REQUIRE(list);
    Json all = Json::parse(list->body);
    CHECK(all.size() == 16);
    CHECK(all[0]["clave"] == "gas_permanente");
    CHECK(all[0]["casas_con_valor"] == 0);
    CHECK(all[0]["badge"]["rol"] == "secundario");

    // alta
    auto created = json_post("/api/caracteristicas", feature_payload("Vista al mar", "bool"));
    REQUIRE(created);
    CHECK(created->status == 201);
    Json c = Json::parse(created->body);
    CHECK(c["clave"] == "vista_al_mar");
    const std::int64_t id = c["id"];

    auto bad = json_post("/api/caracteristicas", feature_payload("", "opcion"));
    REQUIRE(bad);
    CHECK(bad->status == 400);
    Json err = Json::parse(bad->body)["error"];
    CHECK(err["fields"].contains("etiqueta"));
    CHECK(err["fields"].contains("opciones"));

    // edicion
    auto put = json_put("/api/caracteristicas/" + std::to_string(id), Json{{"etiqueta", "Vista panor\xC3\xA1mica"}, {"badge", {{"rol", "terciario"}}}});
    REQUIRE(put);
    CHECK(put->status == 200);
    CHECK(Json::parse(put->body)["etiqueta"] == "Vista panor\xC3\xA1mica");
    CHECK(json_put("/api/caracteristicas/99999", Json{{"etiqueta", "x"}})->status == 404);

    // reordenar: la nueva primera
    auto ord = json_put("/api/caracteristicas/orden", Json{{"ids", Json::array({id})}});
    REQUIRE(ord);
    CHECK(ord->status == 200);
    CHECK(Json::parse(ord->body)[0]["id"] == id);
    CHECK(json_put("/api/caracteristicas/orden", Json{{"ids", "no"}})->status == 400);
    CHECK(json_put("/api/caracteristicas/orden", Json{{"ids", Json::array({424242})}})->status == 400);

    // eliminacion en uso: conteo en GET y casas afectadas en DELETE
    for (int i = 0; i < 3; ++i) e.set_value(e.add_house(), id, "1");
    auto one = cli.Get("/api/caracteristicas/" + std::to_string(id));
    REQUIRE(one);
    CHECK(Json::parse(one->body)["casas_con_valor"] == 3);

    auto gas_del = cli.Delete("/api/caracteristicas/" + std::to_string(e.by_key("gas_permanente").id));
    REQUIRE(gas_del);
    CHECK(gas_del->status == 409);

    auto type_change = json_put("/api/caracteristicas/" + std::to_string(id), Json{{"tipo", "texto"}});
    REQUIRE(type_change);
    CHECK(type_change->status == 409);

    auto del = cli.Delete("/api/caracteristicas/" + std::to_string(id));
    REQUIRE(del);
    CHECK(del->status == 200);
    CHECK(Json::parse(del->body)["casas_afectadas"] == 3);
    CHECK(cli.Get("/api/caracteristicas/" + std::to_string(id))->status == 404);

    auto junk = cli.Post("/api/caracteristicas", "no json", "application/json");
    REQUIRE(junk);
    CHECK(junk->status == 400);
    server.stop();
}
