#include "doctest.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "document_env.hpp"
#include "documents/document_exporter.hpp"
#include "documents/document_renderer.hpp"
#include "documents/export_files.hpp"
#include "http/routes_documents.hpp"
#include "http/server.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace wcdoc;
using namespace photo_test;
using namespace std::chrono_literals;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::vector<fs::path> children(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) out.push_back(e.path());
    return out;
}

// PDF falso: escribe un archivo %PDF o falla con el motivo indicado para los formatos dados.
struct FakePdf : PdfGenerator {
    std::vector<std::string> calls;
    std::vector<int> pages;
    std::set<std::string> failing;  // nombres de archivo html (p. ej. "landscape-16x9.html")
    std::string fail_reason = "fallo_edge";

    PdfResult generate(const fs::path& html, const fs::path& pdf, int page_count) override {
        calls.push_back(html.filename().string());
        pages.push_back(page_count);
        PdfResult r;
        if (failing.count(html.filename().string())) {
            r.reason = fail_reason;
            r.message = "fallo simulado";
            return r;
        }
        write_text_file(pdf, "%PDF-1.4 falso");
        r.ok = true;
        return r;
    }
};

// Se queda esperando hasta que se libere, para probar dos exportaciones a la vez.
struct BlockingPdf : PdfGenerator {
    std::mutex m;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;

    PdfResult generate(const fs::path&, const fs::path& pdf, int) override {
        {
            std::unique_lock<std::mutex> lock(m);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return released; });
        }
        write_text_file(pdf, "%PDF-1.4 falso");
        PdfResult r;
        r.ok = true;
        return r;
    }
    void wait_entered() {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, 10s, [&] { return entered; });
    }
    void release() {
        {
            std::lock_guard<std::mutex> lock(m);
            released = true;
        }
        cv.notify_all();
    }
};

struct ExportEnv : DocEnv {
    DocumentRenderer renderer{engine};
    fs::path outputs;
    FakePdf pdf;
    std::unique_ptr<DocumentExporter> exporter;

    explicit ExportEnv(const char* name) : DocEnv(name) {
        outputs = dir / "salidas";
        exporter = std::make_unique<DocumentExporter>(*builder, renderer, *documents, formats, outputs, pdf);
    }
    std::int64_t flyer(std::int64_t house_id) {
        return documents->create(Json{{"tipo", "flyer"}, {"casa_id", house_id}})["id"].get<std::int64_t>();
    }
};

}  // namespace

TEST_CASE("exportar: flyer en los tres formatos genera carpeta, HTML, PDF, assets y resultado.json") {
    ExportEnv e("wc_exp_full");
    const auto h = e.houses->create(Json{{"titulo", "Casa en Miramar"}, {"precio", 85000}, {"moneda", "USD"}})["id"].get<std::int64_t>();
    const auto photo = e.photo(h);
    const Json result = e.exporter->export_document(e.flyer(h), Json::object());

    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());
    CHECK(folder.parent_path() == e.outputs);
    CHECK(folder.filename().string().find("_casa-en-miramar_flyer") != std::string::npos);
    REQUIRE(result["formatos"].size() == 3);
    for (const Json& f : result["formatos"]) {
        const std::string id = f["id"];
        CHECK(fs::path(f["html"].get<std::string>()) == folder / (id + ".html"));
        CHECK(fs::is_regular_file(folder / (id + ".html")));
        CHECK(fs::is_regular_file(folder / (id + ".pdf")));
        CHECK(f["pdf_estado"] == "generado");
        CHECK(f["pdf_motivo"].is_null());
        const std::string html = slurp(folder / (id + ".html"));
        CHECK(has(html, "85,000 USD"));
        CHECK(has(html, "Guardar como PDF"));
        CHECK(has(html, "assets/fotos/" + std::to_string(photo) + ".jpg"));
        CHECK_FALSE(has(html, "/media/"));
        CHECK_FALSE(has(html, "http://127.0.0.1"));
        CHECK_FALSE(has(html, "localhost"));
        CHECK(has(html, "--color-primario"));  // CSS en linea
        CHECK_FALSE(has(html, "<link"));
    }
    CHECK(fs::is_regular_file(folder / "assets" / "fotos" / (std::to_string(photo) + ".jpg")));
    CHECK(fs::is_regular_file(folder / "assets" / "fuentes" / "Inter-Regular.ttf"));
    const Json saved = Json::parse(slurp(folder / "resultado.json"));
    CHECK(saved["formatos"].size() == 3);
    CHECK(saved["tipo"] == "flyer");
    // el PDF se pidio con el numero de paginas del documento
    CHECK(e.pdf.pages == std::vector<int>{1, 1, 1});
}

TEST_CASE("exportar: el HTML exportado no depende de la app (rutas relativas y fuentes en assets)") {
    ExportEnv e("wc_exp_selfcontained");
    const auto h = e.house("Casa");
    e.photo(h);
    const Json result = e.exporter->export_document(e.flyer(h), Json{{"formatos", Json::array({"instagram-4x5"})}});
    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());
    const std::string html = slurp(folder / "instagram-4x5.html");
    // todas las referencias src= y url() apuntan a assets/ y existen
    std::size_t pos = 0;
    int refs = 0;
    while ((pos = html.find("assets/", pos)) != std::string::npos) {
        const std::size_t end = html.find_first_of("\")", pos);
        const std::string rel = html.substr(pos, end - pos);
        CHECK_MESSAGE(fs::is_regular_file(folder / fs::u8path(rel)), rel);
        pos = end;
        ++refs;
    }
    CHECK(refs >= 3);
    CHECK_FALSE(has(html, "src=\"/"));
    CHECK_FALSE(has(html, "url(\"/"));
    CHECK_FALSE(has(html, "http://"));  // solo hay https del enlace al mapa, si lo hubiera
}

TEST_CASE("exportar: catalogo cuenta una pagina por casa mas la portada para el PDF") {
    ExportEnv e("wc_exp_pages");
    const auto a = e.house("A"), b = e.house("B"), c = e.house("C");
    const Json cat = e.catalog("Catalogo", {a, b, c});
    e.exporter->export_document(cat["id"].get<std::int64_t>(), Json{{"formatos", Json::array({"landscape-16x9"})}});
    CHECK(e.pdf.pages == std::vector<int>{4});
}

TEST_CASE("exportar: formatos elegidos, desconocidos o vacios") {
    ExportEnv e("wc_exp_formats");
    const auto h = e.house("Casa");
    const auto id = e.flyer(h);
    Json one = e.exporter->export_document(id, Json{{"formatos", Json::array({"movil-9x16", "movil-9x16"})}});
    REQUIRE(one["formatos"].size() == 1);
    CHECK(one["formatos"][0]["id"] == "movil-9x16");
    CHECK_THROWS_AS(e.exporter->export_document(id, Json{{"formatos", Json::array({"tiktok"})}}), ValidationFailed);
    CHECK_THROWS_AS(e.exporter->export_document(id, Json{{"formatos", Json::array()}}), ValidationFailed);
    CHECK_THROWS_AS(e.exporter->export_document(999, Json::object()), NotFoundError);
    // los formatos preferidos del documento son el valor por defecto
    e.documents->update(id, Json{{"formatos_preferidos", Json::array({"landscape-16x9", "instagram-4x5"})}});
    Json pref = e.exporter->export_document(id, Json::object());
    REQUIRE(pref["formatos"].size() == 2);
    CHECK(pref["formatos"][0]["id"] == "instagram-4x5");  // en el orden del catalogo de formatos
    CHECK(pref["formatos"][1]["id"] == "landscape-16x9");
}

TEST_CASE("exportar: Edge ausente conserva los HTML y informa el motivo por formato") {
    ExportEnv e("wc_exp_noedge");
    EdgePdfGenerator::Options o;
    o.find_edge = [] { return std::optional<fs::path>{}; };
    o.profile_dir = e.dir / "perfil";
    EdgePdfGenerator no_edge(o);
    DocumentExporter exporter(*e.builder, e.renderer, *e.documents, e.formats, e.outputs, no_edge);
    const auto h = e.house("Casa");
    const Json result = exporter.export_document(e.flyer(h), Json::object());
    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());
    REQUIRE(result["formatos"].size() == 3);
    for (const Json& f : result["formatos"]) {
        const std::string id = f["id"];
        CHECK(fs::is_regular_file(folder / (id + ".html")));
        CHECK_FALSE(fs::exists(folder / (id + ".pdf")));
        CHECK(f["pdf_estado"] == "fallido");
        CHECK(f["pdf_motivo"] == "edge_no_encontrado");
        CHECK(f["pdf"].is_null());
        CHECK_FALSE(f["pdf_mensaje"].get<std::string>().empty());
    }
}

TEST_CASE("exportar: el fallo del PDF de un formato no impide los demas") {
    ExportEnv e("wc_exp_partial");
    e.pdf.failing = {"landscape-16x9.html"};
    e.pdf.fail_reason = "tiempo_agotado";
    const auto h = e.house("Casa");
    const Json result = e.exporter->export_document(e.flyer(h), Json::object());
    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());
    CHECK(e.pdf.calls.size() == 3);  // se intentaron los tres
    for (const Json& f : result["formatos"]) {
        if (f["id"] == "landscape-16x9") {
            CHECK(f["pdf_estado"] == "fallido");
            CHECK(f["pdf_motivo"] == "tiempo_agotado");
            CHECK(fs::is_regular_file(folder / "landscape-16x9.html"));
            CHECK_FALSE(fs::exists(folder / "landscape-16x9.pdf"));
        } else {
            CHECK(f["pdf_estado"] == "generado");
            CHECK(fs::is_regular_file(folder / (f["id"].get<std::string>() + ".pdf")));
        }
    }
}

TEST_CASE("exportar: dos exportaciones seguidas crean carpetas distintas y no tocan la primera") {
    ExportEnv e("wc_exp_twice");
    const auto h = e.house("Casa");
    const auto id = e.flyer(h);
    const Json first = e.exporter->export_document(id, Json::object());
    const fs::path f1 = fs::u8path(first["carpeta"].get<std::string>());
    const std::string html_before = slurp(f1 / "instagram-4x5.html");
    const Json second = e.exporter->export_document(id, Json::object());
    const fs::path f2 = fs::u8path(second["carpeta"].get<std::string>());
    CHECK(f1 != f2);
    CHECK(fs::is_directory(f1));
    CHECK(fs::is_directory(f2));
    CHECK(slurp(f1 / "instagram-4x5.html") == html_before);
    CHECK(children(e.outputs).size() == 2);
}

TEST_CASE("exportar: un error antes de los HTML borra la carpeta parcial") {
    ExportEnv e("wc_exp_cleanup");
    // catalogo sin casas
    const auto h = e.house("Casa");
    const Json cat = e.catalog("Cat", {h});
    e.houses->remove(h);
    CHECK_THROWS_AS(e.exporter->export_document(cat["id"].get<std::int64_t>(), Json::object()), ValidationFailed);
    CHECK(children(e.outputs).empty());

    // plantilla rota
    const fs::path tpl = e.dir / "templates";
    fs::copy(fs::path(WCATALOG_SOURCE_DIR) / "templates", tpl, fs::copy_options::recursive);
    write_text_file(tpl / "flyer.html", "{{ variable_inexistente }}");
    TemplateEngine broken(tpl);
    DocumentRenderer renderer(broken);
    DocumentExporter exporter(*e.builder, renderer, *e.documents, e.formats, e.outputs, e.pdf);
    const auto h2 = e.house("Otra");
    CHECK_THROWS_AS(exporter.export_document(e.flyer(h2), Json::object()), TemplateError);
    CHECK(children(e.outputs).empty());
    CHECK(e.pdf.calls.empty());

    // un archivo de assets que falta
    const auto h3 = e.house("Con foto");
    const auto photo = e.photo(h3);
    fs::remove(e.dir / "fotos" / std::to_string(h3) / (std::to_string(photo) + "_opt.jpg"));
    CHECK_THROWS_AS(e.exporter->export_document(e.flyer(h3), Json::object()), std::runtime_error);
    CHECK(children(e.outputs).empty());
}

TEST_CASE("exportar: la exportacion es una instantanea; editar despues no cambia lo exportado") {
    ExportEnv e("wc_exp_snapshot");
    const auto h = e.houses->create(Json{{"titulo", "Casa"}, {"precio", 85000}, {"moneda", "USD"}})["id"].get<std::int64_t>();
    const auto id = e.flyer(h);
    const Json result = e.exporter->export_document(id, Json{{"formatos", Json::array({"instagram-4x5"})}});
    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());
    const std::string before = slurp(folder / "instagram-4x5.html");
    CHECK(has(before, "85,000 USD"));
    CHECK(has(before, "#1F3A5F"));

    e.houses->update(h, Json{{"titulo", "Casa"}, {"precio", 99000}, {"moneda", "USD"}});
    e.settings->update(Json{{"tema", Json{{"colorPrimario", "#AA0000"}}}});

    CHECK(slurp(folder / "instagram-4x5.html") == before);
    // la vista previa si cambia
    DocumentSnapshot preview = e.builder->snapshot(id, RenderMode::Preview);
    const std::string html = e.renderer.render(DocumentContextBuilder::for_format(preview.context, *e.formats.find("instagram-4x5")));
    CHECK(has(html, "99,000 USD"));
    CHECK(has(html, "#AA0000"));
    CHECK_FALSE(has(html, "85,000"));
}

TEST_CASE("POST /api/documentos/{id}/exportar: exportacion completa y rechazo de dos peticiones simultaneas") {
    ExportEnv e("wc_exp_http");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "<html></html>";
    BlockingPdf blocking;
    DocumentExporter exporter(*e.builder, e.renderer, *e.documents, e.formats, e.outputs, blocking);
    HttpServer server(e.dir / "web");
    register_export_routes(server.raw(), exporter);
    const int port = server.start();

    const auto h = e.house("Casa");
    const std::string url = "/api/documentos/" + std::to_string(e.flyer(h)) + "/exportar";
    const std::string body = Json{{"formatos", Json::array({"instagram-4x5"})}}.dump();

    httplib::Result first;
    std::thread t([&] {
        httplib::Client c1("127.0.0.1", port);
        c1.set_read_timeout(30, 0);
        first = c1.Post(url, body, "application/json");
    });
    blocking.wait_entered();

    httplib::Client c2("127.0.0.1", port);
    auto second = c2.Post(url, body, "application/json");
    REQUIRE(second);
    CHECK(second->status == 409);
    CHECK(Json::parse(second->body)["error"]["code"] == "exportacion_en_curso");

    blocking.release();
    t.join();
    REQUIRE(first);
    CHECK(first->status == 200);
    const Json result = Json::parse(first->body);
    CHECK(result["formatos"].size() == 1);
    CHECK(result["formatos"][0]["pdf_estado"] == "generado");
    // solo se hizo una exportacion
    CHECK(children(e.outputs).size() == 1);

    // y ya se puede volver a exportar
    httplib::Client c3("127.0.0.1", port);
    auto third = c3.Post(url, body, "application/json");
    REQUIRE(third);
    CHECK(third->status == 200);
    CHECK(children(e.outputs).size() == 2);

    auto invalid = c3.Post(url, Json{{"formatos", Json::array()}}.dump(), "application/json");
    REQUIRE(invalid);
    CHECK(invalid->status == 400);
    server.stop();
}

TEST_CASE("POST /api/sistema/abrir-ruta: solo carpetas, HTML y PDF dentro de data/salidas") {
    ExportEnv e("wc_exp_open");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "<html></html>";
    std::vector<fs::path> opened;
    HttpServer server(e.dir / "web");
    register_open_path_routes(server.raw(), e.outputs, [&](const fs::path& p) {
        opened.push_back(p);
        return true;
    });
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    auto open = [&](const fs::path& p) { return cli.Post("/api/sistema/abrir-ruta", Json{{"ruta", to_utf8(p)}}.dump(), "application/json"); };

    const auto h = e.house("Casa");
    const Json result = e.exporter->export_document(e.flyer(h), Json{{"formatos", Json::array({"instagram-4x5"})}});
    const fs::path folder = fs::u8path(result["carpeta"].get<std::string>());

    CHECK(open(folder)->status == 200);
    CHECK(open(folder / "instagram-4x5.html")->status == 200);
    CHECK(open(folder / "instagram-4x5.pdf")->status == 200);
    CHECK(open(e.outputs)->status == 200);
    CHECK(opened.size() == 4);

    // fuera de salidas
    write_text_file(e.dir / "secreto.html", "x");
    CHECK(open(e.dir)->status == 400);
    CHECK(open(e.dir / "secreto.html")->status == 400);
    CHECK(open(e.dir / "c.db")->status == 400);
    CHECK(open(fs::path("C:\\Windows\\System32\\cmd.exe"))->status == 400);
    CHECK(open(folder / ".." / ".." / "secreto.html")->status == 400);
    CHECK(open(folder / ".." / "..")->status == 400);
    // dentro de salidas pero de otro tipo
    CHECK(open(folder / "resultado.json")->status == 400);
    CHECK(open(folder / "no-existe.html")->status == 400);
    // ruta relativa y cuerpo invalido
    CHECK(open(fs::path("salidas"))->status == 400);
    CHECK(cli.Post("/api/sistema/abrir-ruta", "{}", "application/json")->status == 400);
    CHECK(opened.size() == 4);
    server.stop();
}
