#include "doctest.h"

#include <cstdlib>
#include <fstream>
#include <regex>

#include "document_env.hpp"
#include "documents/document_renderer.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace wcdoc;
using namespace photo_test;

namespace {

std::string render(DocEnv& e, std::int64_t doc_id, const char* format = "instagram-4x5",
                   RenderMode mode = RenderMode::Preview) {
    DocumentSnapshot snap = e.builder->snapshot(doc_id, mode);
    DocumentRenderer renderer(e.engine);
    std::string html = renderer.render(DocumentContextBuilder::for_format(snap.context, *e.formats.find(format)));
    // WCATALOG_DUMP_DIR: guarda el HTML de cada render para revisarlo a mano
    if (const char* dump = std::getenv("WCATALOG_DUMP_DIR")) {
        std::ofstream(fs::path(dump) / (std::to_string(doc_id) + "_" + format + "_" + e.dir.filename().string() + ".html"),
                      std::ios::binary)
            << html;
    }
    return html;
}

std::int64_t flyer_of(DocEnv& e, std::int64_t house_id) {
    return e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", house_id}})["id"].get<std::int64_t>();
}

// Solo el cuerpo del documento: el CSS menciona clases y textos que no deben contar como contenido.
std::string body_of(const std::string& html) {
    const auto pos = html.find("</style>");
    return pos == std::string::npos ? html : html.substr(pos);
}

bool has(const std::string& html, const std::string& needle) { return html.find(needle) != std::string::npos; }

int count(const std::string& html, const std::string& needle) {
    int n = 0;
    for (std::size_t pos = html.find(needle); pos != std::string::npos; pos = html.find(needle, pos + needle.size())) ++n;
    return n;
}

// Contenedores sin nada dentro: señal de una sección que debía haberse omitido.
bool has_empty_block(const std::string& html) {
    // el bloque neutro de la foto es vacío a propósito
    static const std::regex empty_block(R"(<(div|p|ul|span|section|a) class="(?!foto-neutra)[^>]*>\s*</\1>)");
    std::smatch m;
    const std::string body = body_of(html);
    if (std::regex_search(body, m, empty_block)) {
        MESSAGE("bloque vacio: " << m.str());
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("render: flyer de casa minima, sin secciones vacias") {
    DocEnv e("wc_render_min");
    const auto h = e.house("Casa minima");
    const std::string html = render(e, flyer_of(e, h));
    CHECK(has(html, "<!DOCTYPE html>"));
    CHECK(has(html, "<h1 class=\"titulo\">Casa minima</h1>"));
    CHECK(has(html, "foto-neutra"));
    CHECK(has(html, "<div class=\"precio\">Consultar</div>"));
    for (const char* absent : {"class=\"badges\"", "class=\"ubicacion\"", "class=\"contacto\"", "class=\"datos\"",
                               "class=\"descripcion\"", "class=\"etiquetas\"", "barra-pdf", "<img"}) {
        CHECK_MESSAGE(!has(body_of(html), absent), absent);
    }
    CHECK_FALSE(has_empty_block(html));
    CHECK(has(html, "@page { size: 1080px 1350px; margin: 0 }"));
    CHECK(has(html, "class=\"formato-instagram-4x5\""));
    CHECK(count(html, "class=\"pagina casa\"") == 1);
}

TEST_CASE("render: precio oculto no deja ningun texto de precio") {
    DocEnv e("wc_render_hidden");
    const auto h = e.houses->create(Json{{"titulo", "Sin precio"}, {"sin_precio", "ocultar"}})["id"].get<std::int64_t>();
    const std::string html = render(e, flyer_of(e, h));
    CHECK_FALSE(has(html, "class=\"precio\""));
    CHECK_FALSE(has(html, "Consultar"));
    CHECK_FALSE(has_empty_block(html));
}

TEST_CASE("render: flyer completo con ubicacion, mapa, datos, contacto y fotos") {
    DocEnv e("wc_render_full");
    e.settings->update(Json{{"contacto", Json{{"nombre", "Ana"}, {"telefono", "+53 5555 5555"}}}});
    const auto h = e.houses->create(Json{{"titulo", "Casa en Miramar"},
                                         {"descripcion", "Amplia y luminosa."},
                                         {"operacion", "venta"},
                                         {"precio", 85000},
                                         {"moneda", "USD"},
                                         {"barrio", "Miramar"},
                                         {"lat", 23.1136},
                                         {"lon", -82.3666},
                                         {"cuartos", 3}})["id"].get<std::int64_t>();
    const auto photo = e.photo(h);
    const std::string html = render(e, flyer_of(e, h));
    CHECK(has(html, "<span class=\"etiqueta\">En venta</span>"));
    CHECK(has(html, "<div class=\"precio\">85,000 USD</div>"));
    CHECK(has(html, "<span class=\"texto\">Miramar</span>"));
    CHECK(has(html, "href=\"https://www.google.com/maps?q=23.1136,-82.3666\""));
    CHECK(has(html, "<p class=\"descripcion\">Amplia y luminosa.</p>"));
    CHECK(has(html, "<span class=\"valor\">3</span><span class=\"etiqueta-dato\">Cuartos</span>"));
    CHECK(has(html, "<span class=\"nombre\">Ana</span>"));
    CHECK(has(html, "Tel. +53 5555 5555"));
    CHECK(has(html, "/media/fotos/" + std::to_string(h) + "/" + std::to_string(photo) + "_opt.jpg"));
    CHECK_FALSE(has(html, "foto-neutra\""));
    CHECK_FALSE(has_empty_block(html));
    // preview: sin la barra de PDF
    CHECK_FALSE(has(body_of(html), "Guardar como PDF"));
    // el CSS base y el del tema van dentro del HTML
    CHECK(has(html, "--color-primario: #1F3A5F"));
    CHECK(has(html, ".barra-pdf"));
    CHECK(has(html, "@media print"));
    CHECK(has(html, "font-family: \"Playfair Display\""));
}

TEST_CASE("render: 14 badges con maximo 8 muestran 8 y '+6'") {
    DocEnv e("wc_render_badges");
    Json values = Json::array();
    for (int i = 0; i < 14; ++i) {
        Feature f = e.features->create(Json{{"etiqueta", "Extra " + std::to_string(i)},
                                            {"categoria", "otra"},
                                            {"tipo", "bool"},
                                            {"badge", Json{{"visible", true}, {"rol", "secundario"}}}});
        values.push_back(Json{{"caracteristica_id", f.id}, {"valor", true}});
    }
    const auto h = e.houses->create(Json{{"titulo", "Casa"}, {"caracteristicas", values}})["id"].get<std::int64_t>();
    const std::string html = render(e, flyer_of(e, h), "instagram-4x5");
    CHECK(count(html, "class=\"badge secundario\"") == 8);
    CHECK(has(html, "<span class=\"badge mas\">+6</span>"));
    CHECK(count(html, "class=\"pagina casa\"") == 1);
}

TEST_CASE("render: el texto del usuario se escapa") {
    DocEnv e("wc_render_escape");
    const auto h = e.house("<script>alert(1)</script> & \"Casa\"", "<b>negrita</b>");
    const std::string html = render(e, flyer_of(e, h));
    CHECK_FALSE(has(html, "<script>alert"));
    CHECK(has(html, "&lt;script&gt;alert(1)&lt;/script&gt; &amp; &quot;Casa&quot;"));
    CHECK(has(html, "&lt;b&gt;negrita&lt;/b&gt;"));
}

TEST_CASE("render: catalogo con portada y una pagina por casa") {
    DocEnv e("wc_render_cat");
    const auto a = e.house("Casa A", "Descripcion A"), b = e.house("Casa B"), c = e.house("Casa C");
    const auto pa = e.photo(a);
    Json cat = e.documents->create(Json{{"tipo", "catalogo"},
                                        {"titulo", "Casas en La Habana"},
                                        {"descripcion", "Una seleccion"},
                                        {"casas", Json::array({Json{{"casa_id", c}}, Json{{"casa_id", a}, {"descripcion_corta", "Frente al mar"}}, Json{{"casa_id", b}}})}});
    const auto id = cat["id"].get<std::int64_t>();
    e.documents->set_cover_photo(id, Json{{"foto_id", pa}});

    const std::string html = render(e, id, "landscape-16x9");
    CHECK(count(html, "class=\"pagina portada\"") == 1);
    CHECK(count(html, "class=\"pagina casa\"") == 3);
    CHECK(has(html, "<h1 class=\"titulo\">Casas en La Habana</h1>"));
    CHECK(has(html, "<p class=\"descripcion\">Una seleccion</p>"));
    CHECK(has(html, "3 propiedades"));
    CHECK(has(html, "<p class=\"descripcion\">Frente al mar</p>"));
    // orden: C, A, B
    CHECK(html.find("Casa C") < html.find("Casa A"));
    CHECK(html.find("Casa A") < html.find("Casa B"));
    // la portada usa la foto elegida
    const std::string cover = "/media/fotos/" + std::to_string(a) + "/" + std::to_string(pa) + "_opt.jpg";
    CHECK(has(html, "<div class=\"foto\">\n    <img src=\"" + cover));
    CHECK(has(html, "@page { size: 1920px 1080px; margin: 0 }"));
    CHECK_FALSE(has_empty_block(html));
}

TEST_CASE("render: catalogo sin portada ni descripcion usa el bloque neutro y omite el texto") {
    DocEnv e("wc_render_cat2");
    const auto a = e.house("Casa A");
    Json cat = e.catalog("Solo una", {a});
    const std::string html = render(e, cat["id"].get<std::int64_t>(), "movil-9x16");
    CHECK(has(html, "class=\"foto-neutra\""));
    CHECK(has(html, "1 propiedad<"));
    CHECK_FALSE(has(body_of(html), "propiedades"));
    CHECK_FALSE(has(body_of(html), "class=\"descripcion\""));
    CHECK_FALSE(has_empty_block(html));
}

TEST_CASE("render: modo export incluye la barra Guardar como PDF y rutas relativas") {
    DocEnv e("wc_render_export");
    const auto h = e.house("Casa");
    e.photo(h);
    const std::string html = render(e, flyer_of(e, h), "instagram-4x5", RenderMode::Export);
    CHECK(has(html, "Guardar como PDF"));
    CHECK(has(html, "class=\"barra-pdf no-print\""));
    CHECK(has(html, "window.print()"));
    CHECK(has(html, "src=\"assets/fotos/"));
    CHECK(has(html, "url(\"assets/fuentes/"));
    CHECK_FALSE(has(html, "/media/"));
    CHECK_FALSE(has(html, "http://127.0.0.1"));
    CHECK_FALSE(has(html, "http://localhost"));
}

TEST_CASE("render: cada formato aplica su clase de composicion") {
    DocEnv e("wc_render_formats");
    const auto h = e.house("Casa");
    const auto id = flyer_of(e, h);
    CHECK(has(render(e, id, "instagram-4x5"), "class=\"formato-instagram-4x5\""));
    CHECK(has(render(e, id, "landscape-16x9"), "class=\"formato-landscape-16x9\""));
    const std::string mobile = render(e, id, "movil-9x16");
    CHECK(has(mobile, "class=\"formato-movil-9x16\""));
    CHECK(has(mobile, "width: 1080px; height: 1920px"));
}

// ---------- GET /render/{id}/{formato} ----------

#include <httplib.h>

#include "http/routes_documents.hpp"
#include "http/server.hpp"

namespace {

// Copia templates/ a una carpeta temporal para poder romperla sin tocar el original.
fs::path copy_templates(const fs::path& to) {
    fs::remove_all(to);
    fs::copy(fs::path(WCATALOG_SOURCE_DIR) / "templates", to, fs::copy_options::recursive);
    return to;
}

}  // namespace

TEST_CASE("GET /render: devuelve el HTML de la vista previa, sin cache, y valida el formato") {
    DocEnv e("wc_render_http");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "<html></html>";
    const auto h = e.house("Casa en Miramar");
    const auto id = flyer_of(e, h);
    DocumentRenderer renderer(e.engine);
    HttpServer server(e.dir / "web");
    register_render_routes(server.raw(), *e.builder, renderer, e.formats);
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto ok = cli.Get("/render/" + std::to_string(id) + "/landscape-16x9");
    REQUIRE(ok);
    CHECK(ok->status == 200);
    CHECK(ok->get_header_value("Content-Type") == "text/html; charset=utf-8");
    CHECK(ok->get_header_value("Cache-Control") == "no-store");
    CHECK(has(ok->body, "Casa en Miramar"));
    CHECK(has(ok->body, "@page { size: 1920px 1080px; margin: 0 }"));

    auto bad_format = cli.Get("/render/" + std::to_string(id) + "/tiktok");
    REQUIRE(bad_format);
    CHECK(bad_format->status == 400);
    auto missing = cli.Get("/render/9999/instagram-4x5");
    REQUIRE(missing);
    CHECK(missing->status == 404);
    server.stop();
}

TEST_CASE("GET /render: una plantilla rota da un error claro con archivo y linea") {
    DocEnv e("wc_render_broken");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "<html></html>";
    const fs::path tpl = copy_templates(e.dir / "templates");
    {
        std::ofstream out(tpl / "parciales" / "_precio.html", std::ios::binary | std::ios::trunc);
        out << "<div>\n{% if casa.precio_texto %}\n<b>{{ casa.precio_texto }}</b>\n";  // falta {% endif %}
    }
    TemplateEngine broken(tpl);
    const auto h = e.house("Casa");
    const auto id = flyer_of(e, h);
    DocumentRenderer renderer(broken);
    HttpServer server(e.dir / "web");
    register_render_routes(server.raw(), *e.builder, renderer, e.formats);
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);

    auto res = cli.Get("/render/" + std::to_string(id) + "/instagram-4x5");
    REQUIRE(res);
    CHECK(res->status == 500);
    CHECK(has(res->body, "No se pudo generar la vista previa"));
    CHECK(has(res->body, "parciales/_precio.html"));
    CHECK(has(res->body, "l\xC3\xADnea"));

    // el motor tambien lo informa directamente
    try {
        DocumentSnapshot snap = e.builder->snapshot(id, RenderMode::Preview);
        renderer.render(DocumentContextBuilder::for_format(snap.context, *e.formats.find("instagram-4x5")));
        FAIL("debia fallar");
    } catch (const TemplateError& err) {
        const std::string msg = err.what();
        CHECK(has(msg, "parciales/_precio.html"));
        CHECK(has(msg, "l\xC3\xADnea"));
    }
    server.stop();
}

TEST_CASE("GET /render: un error de datos en la plantilla tambien se informa") {
    DocEnv e("wc_render_baddata");
    const fs::path tpl = copy_templates(e.dir / "templates");
    {
        std::ofstream out(tpl / "flyer.html", std::ios::binary | std::ios::trunc);
        out << "<p>{{ variable_que_no_existe }}</p>\n";
    }
    TemplateEngine engine(tpl);
    const auto h = e.house("Casa");
    DocumentRenderer renderer(engine);
    DocumentSnapshot snap = e.builder->snapshot(flyer_of(e, h), RenderMode::Preview);
    try {
        renderer.render(DocumentContextBuilder::for_format(snap.context, *e.formats.find("instagram-4x5")));
        FAIL("debia fallar");
    } catch (const TemplateError& err) {
        CHECK(has(err.what(), "flyer.html"));
        CHECK(has(err.what(), "variable_que_no_existe"));
    }
}
