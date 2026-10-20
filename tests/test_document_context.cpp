#include "doctest.h"

#include "document_env.hpp"
#include "documents/document_text.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace wcdoc;
using namespace photo_test;

namespace {

Json snapshot_for(DocEnv& e, std::int64_t doc_id, const char* format = "instagram-4x5", RenderMode mode = RenderMode::Preview) {
    DocumentSnapshot snap = e.builder->snapshot(doc_id, mode);
    return DocumentContextBuilder::for_format(snap.context, *e.formats.find(format));
}

std::int64_t flyer_of(DocEnv& e, std::int64_t house_id) {
    return e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", house_id}})["id"].get<std::int64_t>();
}

}  // namespace

TEST_CASE("importes: miles con coma y decimales solo si los hay") {
    CHECK(format_amount(8500000) == "85,000");
    CHECK(format_amount(8500050) == "85,000.50");
    CHECK(format_amount(99) == "0.99");
    CHECK(format_amount(0) == "0");
    CHECK(format_amount(123456789000) == "1,234,567,890");
    CHECK(format_price(8500000, "USD") == "85,000 USD");
    CHECK(format_price(1200000, "") == "12,000");
    CHECK(format_number(120) == "120");
    CHECK(format_number(85.5) == "85.5");
    CHECK(format_number(85.456) == "85.46");
    CHECK(html_escape("<a href=\"x\">Tom & 'Jerry'</a>") == "&lt;a href=&quot;x&quot;&gt;Tom &amp; &#39;Jerry&#39;&lt;/a&gt;");
}

TEST_CASE("contexto: casa minima, solo con titulo") {
    DocEnv e("wc_ctx_min");
    const auto h = e.house("Casa minima");
    const Json ctx = snapshot_for(e, flyer_of(e, h));
    CHECK(ctx["documento"]["es_flyer"] == true);
    CHECK(ctx["documento"]["titulo"] == "Casa minima");
    CHECK(ctx["documento"]["descripcion"].is_null());
    CHECK(ctx["modo"] == "preview");
    REQUIRE(ctx["casas"].size() == 1);
    const Json& c = ctx["casas"][0];
    CHECK(c["titulo"] == "Casa minima");
    CHECK(c["precio_texto"] == "Consultar");
    CHECK(c["operacion_texto"].is_null());
    CHECK(c["estado_texto"].is_null());
    CHECK(c["datos"].empty());
    CHECK(c["badges"].empty());
    CHECK(c["badges_restantes"] == 0);
    CHECK(c["fotos"].empty());
    CHECK(c["foto_principal"].is_null());
    CHECK(c["ubicacion"]["texto"].is_null());
    CHECK(c["ubicacion"]["mapa_url"].is_null());
    CHECK(c["ubicacion"]["coordenadas_texto"].is_null());
    CHECK(ctx["contacto"].is_null());
    CHECK(ctx["portada"].is_null());
    CHECK(ctx["formato"]["id"] == "instagram-4x5");
    CHECK(ctx["formato"]["ancho"] == 1080);
}

TEST_CASE("contexto: precio con moneda, 'Consultar' o oculto") {
    DocEnv e("wc_ctx_price");
    const auto priced = e.houses->create(Json{{"titulo", "A"}, {"precio", 85000}, {"moneda", "USD"}})["id"].get<std::int64_t>();
    const auto decimals = e.houses->create(Json{{"titulo", "B"}, {"precio", 1250.5}, {"moneda", "EUR"}})["id"].get<std::int64_t>();
    const auto ask = e.houses->create(Json{{"titulo", "C"}, {"sin_precio", "consultar"}})["id"].get<std::int64_t>();
    const auto hidden = e.houses->create(Json{{"titulo", "D"}, {"sin_precio", "ocultar"}})["id"].get<std::int64_t>();
    CHECK(snapshot_for(e, flyer_of(e, priced))["casas"][0]["precio_texto"] == "85,000 USD");
    CHECK(snapshot_for(e, flyer_of(e, decimals))["casas"][0]["precio_texto"] == "1,250.50 EUR");
    CHECK(snapshot_for(e, flyer_of(e, ask))["casas"][0]["precio_texto"] == "Consultar");
    CHECK(snapshot_for(e, flyer_of(e, hidden))["casas"][0]["precio_texto"].is_null());
}

TEST_CASE("contexto: ubicacion, mapa y datos fijos") {
    DocEnv e("wc_ctx_loc");
    const auto h = e.houses->create(Json{{"titulo", "Casa"},
                                         {"operacion", "venta"},
                                         {"estado", "reservada"},
                                         {"direccion", "Calle 5 #123"},
                                         {"barrio", "Miramar"},
                                         {"municipio", "Playa"},
                                         {"provincia", "La Habana"},
                                         {"lat", 23.1136},
                                         {"lon", -82.3666},
                                         {"cuartos", 3},
                                         {"banos", 2},
                                         {"area_construida_m2", 120},
                                         {"area_terreno_m2", 250.5}})["id"].get<std::int64_t>();
    const Json c = snapshot_for(e, flyer_of(e, h))["casas"][0];
    CHECK(c["operacion_texto"] == "En venta");
    CHECK(c["estado_texto"] == "Reservada");
    CHECK(c["ubicacion"]["texto"] == "Calle 5 #123, Miramar, Playa, La Habana");
    CHECK(c["ubicacion"]["mapa_url"] == "https://www.google.com/maps?q=23.1136,-82.3666");
    CHECK(c["ubicacion"]["coordenadas_texto"] == "23.1136, -82.3666");
    REQUIRE(c["datos"].size() == 4);
    CHECK(c["datos"][0] == Json{{"etiqueta", "Cuartos"}, {"valor", "3"}});
    CHECK(c["datos"][1] == Json{{"etiqueta", "Ba\xC3\xB1os"}, {"valor", "2"}});
    CHECK(c["datos"][2]["valor"] == "120 m\xC2\xB2");
    CHECK(c["datos"][3]["valor"] == "250.5 m\xC2\xB2");
}

TEST_CASE("contexto: 14 badges con un maximo de 8 muestran 8 y un resto de 6") {
    DocEnv e("wc_ctx_badges");
    Json values = Json::array();
    for (int i = 0; i < 14; ++i) {
        Feature f = e.features->create(Json{{"etiqueta", "Extra " + std::to_string(i)},
                                            {"categoria", "otra"},
                                            {"tipo", "bool"},
                                            {"badge", Json{{"visible", true}, {"rol", "secundario"}}}});
        values.push_back(Json{{"caracteristica_id", f.id}, {"valor", true}});
    }
    const auto h = e.houses->create(Json{{"titulo", "Casa"}, {"caracteristicas", values}})["id"].get<std::int64_t>();
    const auto id = flyer_of(e, h);

    const Json ig = snapshot_for(e, id, "instagram-4x5");
    CHECK(ig["casas"][0]["badges"].size() == 8);
    CHECK(ig["casas"][0]["badges_restantes"] == 6);
    CHECK(ig["casas"][0]["badges"][0]["texto"] == "Extra 0");

    const Json ls = snapshot_for(e, id, "landscape-16x9");  // admite 10
    CHECK(ls["casas"][0]["badges"].size() == 10);
    CHECK(ls["casas"][0]["badges_restantes"] == 4);

    // la instantánea completa no se recorta
    DocumentSnapshot snap = e.builder->snapshot(id, RenderMode::Preview);
    CHECK(snap.context["casas"][0]["badges"].size() == 14);
}

TEST_CASE("contexto: descripcion del flyer propia o heredada, y recorte automatico a 180 en el catalogo") {
    DocEnv e("wc_ctx_desc");
    std::string long_text;
    while (long_text.size() < 400) long_text += "lorem ipsum dolor ";
    const auto h = e.house("Casa", long_text);
    const auto flyer = flyer_of(e, h);
    CHECK(snapshot_for(e, flyer)["casas"][0]["descripcion"] == long_text.substr(0, long_text.size() - 1));  // la casa recorta el espacio final
    const std::string shortened = snapshot_for(e, flyer)["casas"][0]["descripcion_corta"];
    CHECK(shortened.size() <= 183);
    CHECK(shortened.substr(shortened.size() - 3) == "\xE2\x80\xA6");

    e.documents->update(flyer, Json{{"titulo", "Oferta"}, {"descripcion", "Texto propio"}});
    const Json own = snapshot_for(e, flyer);
    CHECK(own["documento"]["titulo"] == "Oferta");
    CHECK(own["casas"][0]["titulo"] == "Oferta");
    CHECK(own["casas"][0]["descripcion"] == "Texto propio");
    CHECK(e.houses->get(h)["titulo"] == "Casa");

    Json cat = e.catalog("Cat", {h});
    CHECK(snapshot_for(e, cat["id"].get<std::int64_t>())["casas"][0]["descripcion_corta"] ==
          cat["casas"][0]["descripcion_corta_efectiva"]);
}

TEST_CASE("contexto: contacto solo si hay algun dato") {
    DocEnv e("wc_ctx_contact");
    const auto h = e.house("Casa");
    const auto id = flyer_of(e, h);
    CHECK(snapshot_for(e, id)["contacto"].is_null());
    e.settings->update(Json{{"contacto", Json{{"telefono", "+53 5555 5555"}, {"nombre", "Ana"}}}});
    const Json c = snapshot_for(e, id)["contacto"];
    CHECK(c["telefono"] == "+53 5555 5555");
    CHECK(c["nombre"] == "Ana");
    CHECK(c["email"] == "");
}

TEST_CASE("contexto: fotos con src segun el modo, limite por casa y activos a copiar") {
    DocEnv e("wc_ctx_photos");
    const auto h = e.house("Casa");
    std::vector<std::int64_t> ids;
    for (int i = 0; i < 7; ++i) ids.push_back(e.photo(h));
    const auto flyer = flyer_of(e, h);

    const Json preview = snapshot_for(e, flyer);
    REQUIRE(preview["casas"][0]["fotos"].size() == kFlyerPhotosPerHouse);
    CHECK(preview["casas"][0]["fotos"][0]["src"] ==
          "/media/fotos/" + std::to_string(h) + "/" + std::to_string(ids[0]) + "_opt.jpg");
    CHECK(preview["casas"][0]["foto_principal"]["src"] == preview["casas"][0]["fotos"][0]["src"]);
    CHECK(preview["casas"][0]["fotos"][0]["ancho"].get<int>() > 0);
    CHECK(preview["tema_css"].get<std::string>().find("/media/fuentes/") != std::string::npos);

    DocumentSnapshot exp = e.builder->snapshot(flyer, RenderMode::Export);
    const Json& ctx = exp.context;
    CHECK(ctx["modo"] == "export");
    CHECK(ctx["casas"][0]["fotos"][0]["src"] == "assets/fotos/" + std::to_string(ids[0]) + ".jpg");
    CHECK(ctx["tema_css"].get<std::string>().find("assets/fuentes/") != std::string::npos);
    CHECK(ctx["tema_css"].get<std::string>().find("/media/") == std::string::npos);
    int photo_assets = 0, font_assets = 0;
    for (const AssetFile& a : exp.assets) {
        CHECK(fs::is_regular_file(a.from));
        if (a.to.rfind("fotos/", 0) == 0) ++photo_assets;
        if (a.to.rfind("fuentes/", 0) == 0) ++font_assets;
    }
    CHECK(photo_assets == static_cast<int>(kFlyerPhotosPerHouse));
    CHECK(font_assets >= 2);  // titulos y texto

    // en un catalogo se incluyen menos fotos por casa
    Json cat = e.catalog("Cat", {h});
    CHECK(snapshot_for(e, cat["id"].get<std::int64_t>())["casas"][0]["fotos"].size() == kCatalogPhotosPerHouse);
}

TEST_CASE("contexto: portada del catalogo en vista previa y exportacion") {
    DocEnv e("wc_ctx_cover");
    const auto h = e.house("Casa");
    const auto p = e.photo(h);
    Json cat = e.catalog("Cat", {h});
    const auto id = cat["id"].get<std::int64_t>();
    CHECK(snapshot_for(e, id)["portada"].is_null());

    e.documents->set_cover_photo(id, Json{{"foto_id", p}});
    CHECK(snapshot_for(e, id)["portada"]["src"] == "/media/fotos/" + std::to_string(h) + "/" + std::to_string(p) + "_opt.jpg");
    DocumentSnapshot exp = e.builder->snapshot(id, RenderMode::Export);
    CHECK(exp.context["portada"]["src"] == "assets/fotos/portada.jpg");
    bool has_cover = false;
    for (const AssetFile& a : exp.assets) has_cover = has_cover || a.to == "fotos/portada.jpg";
    CHECK(has_cover);

    const Bytes jpeg = plain_jpeg();
    e.documents->set_cover_upload(id, jpeg.data(), jpeg.size());
    CHECK(snapshot_for(e, id)["portada"]["src"] == "/media/documentos/" + std::to_string(id) + "/portada_opt.jpg");
    DocumentSnapshot exp2 = e.builder->snapshot(id, RenderMode::Export);
    CHECK(exp2.context["portada"]["src"] == "assets/fotos/portada.jpg");
}

TEST_CASE("contexto: un catalogo sin casas se ve en vista previa pero no se exporta") {
    DocEnv e("wc_ctx_empty");
    const auto h = e.house("Casa");
    Json cat = e.catalog("Cat", {h});
    e.houses->remove(h);
    const auto id = cat["id"].get<std::int64_t>();
    CHECK(snapshot_for(e, id)["casas"].empty());
    CHECK_THROWS_AS(e.builder->snapshot(id, RenderMode::Export), ValidationFailed);
}

TEST_CASE("contexto: el tema refleja los ajustes vigentes") {
    DocEnv e("wc_ctx_theme");
    const auto h = e.house("Casa");
    const auto id = flyer_of(e, h);
    CHECK(snapshot_for(e, id)["tema_css"].get<std::string>().find("#1F3A5F") != std::string::npos);
    e.settings->update(Json{{"tema", Json{{"colorPrimario", "#AA0000"}}}});
    const std::string css = snapshot_for(e, id)["tema_css"];
    CHECK(css.find("#AA0000") != std::string::npos);
    CHECK(css.find("#1F3A5F") == std::string::npos);
}
