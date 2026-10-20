#include "doctest.h"

#include <httplib.h>

#include <filesystem>
#include <fstream>

#include "db/migrations.hpp"
#include "documents/document_service.hpp"
#include "documents/document_text.hpp"
#include "documents/formats.hpp"
#include "houses/feature_service.hpp"
#include "houses/house_service.hpp"
#include "http/routes_documents.hpp"
#include "http/routes_houses.hpp"
#include "http/server.hpp"
#include "document_env.hpp"
#include "photos/photo_service.hpp"
#include "settings/settings_service.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace wcdoc;
using namespace photo_test;

namespace {

const std::string kEllipsis = "\xE2\x80\xA6";

}  // namespace

TEST_CASE("descripcion corta: un texto breve queda igual y uno largo se recorta en palabra completa") {
    CHECK(make_short_description("Casa luminosa") == "Casa luminosa");
    CHECK(make_short_description("") == "");
    std::string long_text;
    while (long_text.size() < 400) long_text += "palabra" + std::to_string(long_text.size() % 7) + " ";
    const std::string s = make_short_description(long_text);
    REQUIRE(s.size() >= 3);
    CHECK(s.substr(s.size() - 3) == kEllipsis);
    const std::string body = s.substr(0, s.size() - 3);
    CHECK(body.size() <= 180);
    CHECK(body.size() > 150);
    // termina en una palabra completa: lo que sigue en el original es un espacio
    CHECK(long_text[body.size()] == ' ');
    CHECK(body.back() != ' ');
}

TEST_CASE("descripcion corta: cuenta caracteres y no bytes, y no parte letras con tilde") {
    std::string t;
    for (int i = 0; i < 100; ++i) t += "ni\xC3\xB1o ";  // 500 caracteres
    const std::string s = make_short_description(t);
    CHECK(s.substr(s.size() - 3) == kEllipsis);
    CHECK(s.find("\xC3\xB1o" + kEllipsis) != std::string::npos);  // acaba en palabra completa
    // un solo bloque sin espacios se corta en el límite
    const std::string blob(300, 'x');
    CHECK(make_short_description(blob) == std::string(180, 'x') + kEllipsis);
}

TEST_CASE("slug del titulo: minusculas, sin tildes, con guiones y limitado") {
    CHECK(slugify_title("Casas en La Habana") == "casas-en-la-habana");
    CHECK(slugify_title("\xC2\xA1Oportunidad en Miramar!") == "oportunidad-en-miramar");
    CHECK(slugify_title("Ni\xC3\xB1os y Pe\xC3\xB1" "a") == "ninos-y-pena");
    CHECK(slugify_title("!!!") == "documento");
    CHECK(slugify_title(std::string(100, 'a')).size() == 40);
    CHECK(slugify_title("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa b").back() != '-');
}

TEST_CASE("flyer: se crea desde una casa y hereda su titulo y descripcion") {
    DocEnv e("wc_doc_flyer1");
    const auto h = e.house("Casa en Miramar", "Amplia y luminosa");
    Json f = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", h}});
    CHECK(f["tipo"] == "flyer");
    CHECK(f["titulo"].is_null());
    CHECK(f["titulo_efectivo"] == "Casa en Miramar");
    CHECK(f["descripcion_efectiva"] == "Amplia y luminosa");
    REQUIRE(f["casas"].size() == 1);
    CHECK(f["casas"][0]["casa_id"] == h);
    CHECK(f["portada"].is_null());
    CHECK(e.documents->list()[0]["titulo"] == "Casa en Miramar");
}

TEST_CASE("flyer: el titulo propio no cambia el de la casa") {
    DocEnv e("wc_doc_flyer2");
    const auto h = e.house("Casa en Miramar");
    Json f = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", h}});
    f = e.documents->update(f["id"].get<std::int64_t>(),
                            Json{{"titulo", "\xC2\xA1Oportunidad en Miramar!"}, {"descripcion", "Solo esta semana"}});
    CHECK(f["titulo_efectivo"] == "\xC2\xA1Oportunidad en Miramar!");
    CHECK(f["descripcion_efectiva"] == "Solo esta semana");
    CHECK(e.houses->get(h)["titulo"] == "Casa en Miramar");
    // vaciar el titulo vuelve a heredar
    f = e.documents->update(f["id"].get<std::int64_t>(), Json{{"titulo", ""}, {"descripcion", nullptr}});
    CHECK(f["titulo"].is_null());
    CHECK(f["titulo_efectivo"] == "Casa en Miramar");
    CHECK(f["casas"].size() == 1);  // la casa del flyer no cambia al editar
}

TEST_CASE("flyer: exige una casa existente") {
    DocEnv e("wc_doc_flyer3");
    CHECK(e.errors_of(Json{{"tipo", "flyer"}}).count("casa_id") == 1);
    CHECK(e.errors_of(Json{{"tipo", "flyer"}, {"casa_id", 99}}).count("casa_id") == 1);
    CHECK(e.errors_of(Json{{"tipo", "otro"}}).count("tipo") == 1);
    CHECK(e.documents->list().empty());
}

TEST_CASE("catalogo: guarda las casas en el orden dado y permite reordenarlas") {
    DocEnv e("wc_doc_cat1");
    const auto a = e.house("A"), b = e.house("B"), c = e.house("C");
    Json d = e.catalog("Casas en La Habana", {c, a, b});
    CHECK(d["titulo_efectivo"] == "Casas en La Habana");
    CHECK(DocEnv::house_order(d) == std::vector<std::int64_t>{c, a, b});
    const auto id = d["id"].get<std::int64_t>();
    e.documents->update(id, Json{{"titulo", "Casas en La Habana"}, {"casas", DocEnv::casas({b, c, a})}});
    CHECK(DocEnv::house_order(e.documents->get(id)) == std::vector<std::int64_t>{b, c, a});
}

TEST_CASE("catalogo: titulo obligatorio, al menos una casa y sin repetidas") {
    DocEnv e("wc_doc_cat2");
    const auto a = e.house("A");
    auto no_houses = e.errors_of(Json{{"tipo", "catalogo"}, {"titulo", "X"}, {"casas", Json::array()}});
    REQUIRE(no_houses.count("casas") == 1);
    CHECK(no_houses["casas"].find("al menos una casa") != std::string::npos);
    CHECK(e.errors_of(Json{{"tipo", "catalogo"}, {"titulo", "X"}}).count("casas") == 1);
    CHECK(e.errors_of(Json{{"tipo", "catalogo"}, {"casas", DocEnv::casas({a})}}).count("titulo") == 1);
    CHECK(e.errors_of(Json{{"tipo", "catalogo"}, {"titulo", "   "}, {"casas", DocEnv::casas({a})}}).count("titulo") == 1);
    CHECK(e.errors_of(Json{{"tipo", "catalogo"}, {"titulo", "X"}, {"casas", DocEnv::casas({a, a})}}).count("casas") == 1);
    CHECK(e.errors_of(Json{{"tipo", "catalogo"}, {"titulo", "X"}, {"casas", DocEnv::casas({77})}}).count("casas") == 1);
    CHECK(e.documents->list().empty());
    // editar tampoco permite dejarlo sin casas
    Json d = e.catalog("X", {a});
    CHECK_THROWS_AS(e.documents->update(d["id"].get<std::int64_t>(), Json{{"titulo", "X"}, {"casas", Json::array()}}),
                    ValidationFailed);
}

TEST_CASE("catalogo: descripcion corta propia o automatica de 180 caracteres") {
    DocEnv e("wc_doc_cat3");
    std::string long_text;
    while (long_text.size() < 400) long_text += "lorem ipsum dolor ";
    const auto a = e.house("A", long_text), b = e.house("B", "Corta");
    Json casas = Json::array({Json{{"casa_id", a}}, Json{{"casa_id", b}, {"descripcion_corta", "  A dos cuadras del mar  "}}});
    Json d = e.documents->create(Json{{"tipo", "catalogo"}, {"titulo", "T"}, {"casas", casas}});
    const std::string auto_desc = d["casas"][0]["descripcion_corta_efectiva"];
    CHECK(d["casas"][0]["descripcion_corta"].is_null());
    CHECK(auto_desc.substr(auto_desc.size() - 3) == kEllipsis);
    CHECK(auto_desc.size() <= 183);
    CHECK(d["casas"][1]["descripcion_corta"] == "A dos cuadras del mar");
    CHECK(d["casas"][1]["descripcion_corta_efectiva"] == "A dos cuadras del mar");
}

TEST_CASE("documentos: reflejan siempre los datos vigentes de sus casas") {
    DocEnv e("wc_doc_live");
    const auto a = e.house("Antes");
    Json d = e.catalog("T", {a});
    e.houses->update(a, Json{{"titulo", "Despues"}});
    CHECK(e.documents->get(d["id"].get<std::int64_t>())["casas"][0]["titulo"] == "Despues");
}

TEST_CASE("documentos: listar, abrir inexistente y eliminar") {
    DocEnv e("wc_doc_list");
    const auto a = e.house("A");
    Json d1 = e.catalog("Primero", {a});
    Json d2 = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", a}});
    CHECK(e.documents->list().size() == 2);
    e.documents->remove(d1["id"].get<std::int64_t>());
    REQUIRE(e.documents->list().size() == 1);
    CHECK(e.documents->list()[0]["id"] == d2["id"]);
    CHECK_THROWS_AS(e.documents->get(d1["id"].get<std::int64_t>()), NotFoundError);
    CHECK_THROWS_AS(e.documents->remove(d1["id"].get<std::int64_t>()), NotFoundError);
    CHECK(e.houses->get(a)["titulo"] == "A");  // la casa sigue
}

TEST_CASE("formatos preferidos: solo ids conocidos") {
    DocEnv e("wc_doc_fmt");
    const auto a = e.house("A");
    CHECK(e.errors_of(Json{{"tipo", "flyer"}, {"casa_id", a}, {"formatos_preferidos", Json::array({"nope"})}})
              .count("formatos_preferidos") == 1);
    Json f = e.documents->create(
        Json{{"tipo", "flyer"}, {"casa_id", a}, {"formatos_preferidos", Json::array({"instagram-4x5"})}});
    CHECK(f["formatos_preferidos"] == Json::array({"instagram-4x5"}));
}

TEST_CASE("portada: subida se guarda optimizada y aparece en el documento") {
    DocEnv e("wc_doc_cover1");
    const auto a = e.house("A");
    Json d = e.catalog("T", {a});
    const auto id = d["id"].get<std::int64_t>();
    const Bytes jpeg = plain_jpeg();
    d = e.documents->set_cover_upload(id, jpeg.data(), jpeg.size());
    REQUIRE_FALSE(d["portada"].is_null());
    CHECK(d["portada"]["origen"] == "subida");
    CHECK(d["portada"]["url"] == "/media/documentos/" + std::to_string(id) + "/portada_opt.jpg");
    const fs::path dir = e.dir / "documentos" / std::to_string(id);
    CHECK(fs::is_regular_file(dir / "portada_orig.jpg"));
    CHECK(fs::is_regular_file(dir / "portada_opt.jpg"));
    CHECK(fs::is_regular_file(dir / "portada_min.jpg"));
    CHECK_FALSE(e.documents->cover_file(id).empty());
    // un archivo que no es imagen se rechaza y la portada anterior se conserva
    const Bytes junk = {1, 2, 3, 4};
    CHECK_THROWS_AS(e.documents->set_cover_upload(id, junk.data(), junk.size()), PhotoRejected);
    CHECK(e.documents->get(id)["portada"]["origen"] == "subida");
    // eliminar el documento borra su carpeta
    e.documents->remove(id);
    CHECK_FALSE(fs::exists(dir));
}

TEST_CASE("portada: elegida entre las fotos de las casas incluidas") {
    DocEnv e("wc_doc_cover2");
    const auto a = e.house("A"), b = e.house("B"), outside = e.house("Fuera");
    const auto pa = e.photo(a);
    const auto pout = e.photo(outside);
    Json d = e.catalog("T", {a, b});
    const auto id = d["id"].get<std::int64_t>();
    d = e.documents->set_cover_photo(id, Json{{"foto_id", pa}});
    CHECK(d["portada"]["origen"] == "casa");
    CHECK(d["portada"]["foto_id"] == pa);
    CHECK(d["portada"]["url"] == "/media/fotos/" + std::to_string(a) + "/" + std::to_string(pa) + "_opt.jpg");
    CHECK_THROWS_AS(e.documents->set_cover_photo(id, Json{{"foto_id", pout}}), ValidationFailed);
    CHECK(e.documents->get(id)["portada"]["foto_id"] == pa);
    // quitar la casa del catalogo invalida la portada elegida de esa casa
    e.documents->update(id, Json{{"titulo", "T"}, {"casas", DocEnv::casas({b})}});
    CHECK(e.documents->get(id)["portada"].is_null());
    // elegir una foto reemplaza una portada subida y limpia sus archivos
    const Bytes jpeg = plain_jpeg();
    e.documents->set_cover_upload(id, jpeg.data(), jpeg.size());
    const auto pb = e.photo(b);
    e.documents->set_cover_photo(id, Json{{"foto_id", pb}});
    CHECK(e.documents->cover_file(id).empty());
    CHECK_FALSE(fs::exists(e.dir / "documentos" / std::to_string(id)));
    e.documents->set_cover_photo(id, Json{{"foto_id", nullptr}});
    CHECK(e.documents->get(id)["portada"].is_null());
}

TEST_CASE("portada: si se elimina la foto elegida, el catalogo queda sin portada") {
    DocEnv e("wc_doc_cover3");
    const auto a = e.house("A");
    const auto pa = e.photo(a);
    Json d = e.catalog("T", {a});
    const auto id = d["id"].get<std::int64_t>();
    e.documents->set_cover_photo(id, Json{{"foto_id", pa}});
    REQUIRE_FALSE(e.documents->get(id)["portada"].is_null());
    e.photos->remove(pa);
    Json after = e.documents->get(id);
    CHECK(after["portada"].is_null());
    CHECK(after["casas"].size() == 1);
}

TEST_CASE("portada: un flyer no tiene portada") {
    DocEnv e("wc_doc_cover4");
    const auto a = e.house("A");
    Json f = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", a}});
    const Bytes jpeg = plain_jpeg();
    const auto id = f["id"].get<std::int64_t>();
    CHECK_THROWS_AS(e.documents->set_cover_upload(id, jpeg.data(), jpeg.size()), ValidationFailed);
    CHECK_THROWS_AS(e.documents->set_cover_photo(id, Json{{"foto_id", nullptr}}), ValidationFailed);
}

TEST_CASE("casa usada en un flyer y un catalogo: el aviso lista ambos y al eliminar cae el flyer y se quita del catalogo") {
    DocEnv e("wc_doc_house_del");
    const auto a = e.house("Casa en Miramar"), b = e.house("Otra");
    Json flyer = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", a}});
    Json other_flyer = e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", b}});
    Json cat = e.catalog("Catalogo", {a, b});
    Json only = e.catalog("Solo A", {a});

    Json warn = e.documents->for_house(a);
    REQUIRE(warn.size() == 3);
    int removed = 0, kept = 0;
    for (const Json& w : warn) {
        if (w["efecto"] == "se_elimina") {
            ++removed;
            CHECK(w["tipo"] == "flyer");
            CHECK(w["titulo"] == "Casa en Miramar");
        } else {
            ++kept;
            CHECK(w["tipo"] == "catalogo");
        }
    }
    CHECK(removed == 1);
    CHECK(kept == 2);
    CHECK(e.documents->for_house(b).size() == 2);

    e.houses->remove(a);
    CHECK_THROWS_AS(e.documents->get(flyer["id"].get<std::int64_t>()), NotFoundError);
    CHECK(e.documents->get(other_flyer["id"].get<std::int64_t>())["casas"].size() == 1);
    CHECK(DocEnv::house_order(e.documents->get(cat["id"].get<std::int64_t>())) == std::vector<std::int64_t>{b});
    // el catalogo que quedo sin casas se conserva
    Json empty = e.documents->get(only["id"].get<std::int64_t>());
    CHECK(empty["casas"].empty());
    CHECK(empty["titulo"] == "Solo A");
    CHECK(e.documents->for_house(a).empty());
}

TEST_CASE("API de documentos: alta de flyer, catalogo sin casas (400), reordenacion y portada") {
    DocEnv e("wc_doc_http");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "<html></html>";
    HttpServer server(e.dir / "web");
    register_house_routes(server.raw(), *e.houses);
    register_document_routes(server.raw(), *e.documents);
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    auto post = [&](const std::string& p, const Json& b) { return cli.Post(p, b.dump(), "application/json"); };
    auto put = [&](const std::string& p, const Json& b) { return cli.Put(p, b.dump(), "application/json"); };

    const auto a = e.house("A"), b = e.house("B");
    auto flyer = post("/api/documentos", Json{{"tipo", "flyer"}, {"casa_id", a}});
    REQUIRE(flyer);
    CHECK(flyer->status == 201);
    CHECK(Json::parse(flyer->body)["titulo_efectivo"] == "A");

    auto empty = post("/api/documentos", Json{{"tipo", "catalogo"}, {"titulo", "X"}, {"casas", Json::array()}});
    REQUIRE(empty);
    CHECK(empty->status == 400);
    CHECK(Json::parse(empty->body)["error"]["fields"].contains("casas"));
    CHECK(post("/api/documentos", Json{{"tipo", "catalogo"}, {"titulo", "X"}, {"casas", DocEnv::casas({a, b})}})->status == 201);

    auto list = cli.Get("/api/documentos");
    REQUIRE(list);
    CHECK(Json::parse(list->body).size() == 2);
    const auto cat_id = Json::parse(list->body)[0]["id"].get<std::int64_t>();

    auto reordered = put("/api/documentos/" + std::to_string(cat_id), Json{{"titulo", "X"}, {"casas", DocEnv::casas({b, a})}});
    REQUIRE(reordered);
    CHECK(reordered->status == 200);
    CHECK(DocEnv::house_order(Json::parse(reordered->body)) == std::vector<std::int64_t>{b, a});

    CHECK(cli.Get("/api/documentos/999")->status == 404);
    auto used = cli.Get("/api/casas/" + std::to_string(a) + "/documentos");
    REQUIRE(used);
    CHECK(Json::parse(used->body).size() == 2);

    // portada subida (multipart) y servida; elegir una foto de casa por JSON
    const Bytes jpeg = plain_jpeg();
    httplib::UploadFormDataItems items = {{"archivo", std::string(jpeg.begin(), jpeg.end()), "p.jpg", "image/jpeg"}};
    auto up = cli.Post("/api/documentos/" + std::to_string(cat_id) + "/portada", items);
    REQUIRE(up);
    CHECK(up->status == 200);
    auto media = cli.Get("/media/documentos/" + std::to_string(cat_id) + "/portada_opt.jpg");
    REQUIRE(media);
    CHECK(media->status == 200);
    CHECK(media->get_header_value("Content-Type") == "image/jpeg");
    CHECK(cli.Get("/media/documentos/" + std::to_string(cat_id) + "/../../c.db")->status == 404);

    const auto pa = e.photo(a);
    auto chosen = post("/api/documentos/" + std::to_string(cat_id) + "/portada", Json{{"foto_id", pa}});
    REQUIRE(chosen);
    CHECK(chosen->status == 200);
    CHECK(Json::parse(chosen->body)["portada"]["origen"] == "casa");
    CHECK(cli.Get("/media/documentos/" + std::to_string(cat_id) + "/portada_opt.jpg")->status == 404);

    CHECK(cli.Delete("/api/documentos/" + std::to_string(cat_id))->status == 200);
    CHECK(cli.Delete("/api/documentos/" + std::to_string(cat_id))->status == 404);
    server.stop();
}
