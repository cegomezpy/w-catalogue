#include "doctest.h"

#include <fstream>
#include <sstream>

#include "document_env.hpp"
#include "documents/document_renderer.hpp"
#include "documents/export_files.hpp"
#include "documents/pdf_generator.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace wcdoc;
using namespace photo_test;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

std::int64_t flyer_of(DocEnv& e, std::int64_t house_id) {
    return e.documents->create(Json{{"tipo", "flyer"}, {"casa_id", house_id}})["id"].get<std::int64_t>();
}

}  // namespace

TEST_CASE("carpeta de salida: fecha-hora, titulo sin tildes y tipo") {
    CHECK(output_folder_name("2026-10-20_1435", "Casas en La Habana", "catalogo") ==
          "2026-10-20_1435_casas-en-la-habana_catalogo");
    CHECK(output_folder_name("2026-10-20_1435", "\xC2\xA1Oportunidad en Miramar!", "flyer") ==
          "2026-10-20_1435_oportunidad-en-miramar_flyer");
    CHECK(output_folder_name("2026-10-20_1435", "???", "flyer") == "2026-10-20_1435_documento_flyer");
    const std::string stamp = current_stamp();
    CHECK(stamp.size() == 15);
    CHECK(stamp[4] == '-');
    CHECK(stamp[10] == '_');
}

TEST_CASE("carpeta de salida: una colision anade sufijo y no modifica la anterior") {
    DocEnv e("wc_export_collision");
    const fs::path outputs = e.dir / "salidas";
    const std::string name = "2026-10-20_1435_casa_flyer";
    const fs::path first = create_output_folder(outputs, name);
    write_text_file(first / "marca.txt", "primera");
    const fs::path second = create_output_folder(outputs, name);
    const fs::path third = create_output_folder(outputs, name);
    CHECK(first.filename().string() == name);
    CHECK(second.filename().string() == name + "-2");
    CHECK(third.filename().string() == name + "-3");
    CHECK(slurp(first / "marca.txt") == "primera");
    CHECK(fs::is_empty(second));
}

TEST_CASE("assets: se copian las fotos usadas, la portada y las fuentes elegidas") {
    DocEnv e("wc_export_assets");
    const auto a = e.house("A"), b = e.house("B");
    const auto pa = e.photo(a);
    e.photo(b);
    Json cat = e.catalog("Cat", {a, b});
    const auto id = cat["id"].get<std::int64_t>();
    e.documents->set_cover_photo(id, Json{{"foto_id", pa}});

    DocumentSnapshot snap = e.builder->snapshot(id, RenderMode::Export);
    const fs::path folder = create_output_folder(e.dir / "salidas", "x");
    copy_assets(folder, snap.assets);

    const fs::path assets = folder / "assets";
    // fotos de las dos casas + portada
    const fs::path src_a = e.dir / "fotos" / std::to_string(a) / (std::to_string(pa) + "_opt.jpg");
    CHECK(slurp(assets / "fotos" / (std::to_string(pa) + ".jpg")) == slurp(src_a));
    CHECK(slurp(assets / "fotos" / "portada.jpg") == slurp(src_a));
    int photos = 0;
    for (const auto& f : fs::directory_iterator(assets / "fotos")) {
        (void)f;
        ++photos;
    }
    CHECK(photos == 3);  // dos fotos y la portada
    // fuentes del tema por defecto: Playfair Display (titulos) e Inter (texto), regular y negrita
    for (const char* font : {"PlayfairDisplay-Regular.ttf", "PlayfairDisplay-Bold.ttf", "Inter-Regular.ttf", "Inter-Bold.ttf"}) {
        CHECK_MESSAGE(fs::is_regular_file(assets / "fuentes" / font), font);
    }
    CHECK_FALSE(fs::exists(assets / "fuentes" / "Poppins-Regular.ttf"));  // las no elegidas no se copian
    CHECK(slurp(assets / "fuentes" / "Inter-Regular.ttf") == slurp(fs::path(WCATALOG_SOURCE_DIR) / "fonts" / "Inter-Regular.ttf"));
}

TEST_CASE("assets: un origen que falta da un error con la ruta") {
    DocEnv e("wc_export_assets_missing");
    const fs::path folder = create_output_folder(e.dir / "salidas", "x");
    CHECK_THROWS_WITH_AS(copy_assets(folder, {{e.dir / "no-existe.jpg", "fotos/1.jpg"}}),
                         doctest::Contains("no-existe.jpg"), std::runtime_error);
}

// ---------- PDF con Edge headless ----------

#include <chrono>
#include <regex>

#include "documents/pdf_generator.hpp"
#include "runtime/edge_launcher.hpp"

namespace {

// Ancho y alto de la primera /MediaBox del PDF, en puntos.
bool media_box(const fs::path& pdf, double& w, double& h) {
    const std::string data = slurp(pdf);
    static const std::regex box(R"(/MediaBox\s*\[\s*0\s+0\s+([0-9.]+)\s+([0-9.]+)\s*\])");
    std::smatch m;
    if (!std::regex_search(data, m, box)) return false;
    w = std::stod(m[1]);
    h = std::stod(m[2]);
    return true;
}

const char* kSimplePage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>@page { size: 1080px 1350px; margin: 0 }"
    "body{margin:0} .p{width:1080px;height:1350px;background:#1F3A5F;color:#fff;font:64px sans-serif}</style></head>"
    "<body><div class=\"p\">Casa en Mar\xC3\xADa \xC3\x91" "and\xC3\xBA</div></body></html>";

}  // namespace

TEST_CASE("PDF: la URL file:/// codifica espacios y caracteres no ASCII") {
    CHECK(path_to_file_url(fs::path("D:\\Proyectos\\w catalog\\a.html")) == "file:///D:/Proyectos/w%20catalog/a.html");
    CHECK(path_to_file_url(fs::u8path("C:\\Mar\xC3\xAD" "a\\x.html")) == "file:///C:/Mar%C3%ADa/x.html");
}

TEST_CASE("PDF: Edge ausente devuelve edge_no_encontrado") {
    Dir t("wc_pdf_noedge");
    EdgePdfGenerator::Options o;
    o.find_edge = [] { return std::optional<fs::path>{}; };
    o.profile_dir = t.path / "perfil";
    EdgePdfGenerator gen(o);
    write_text_file(t.path / "a.html", kSimplePage);
    const PdfResult r = gen.generate(t.path / "a.html", t.path / "a.pdf", 1);
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "edge_no_encontrado");
    CHECK_FALSE(r.message.empty());
    CHECK_FALSE(fs::exists(t.path / "a.pdf"));
}

TEST_CASE("PDF: un proceso que tarda demasiado se detiene y da tiempo_agotado") {
    Dir t("wc_pdf_timeout");
    EdgePdfGenerator::Options o;
    o.find_edge = [] { return std::optional<fs::path>{"C:\\Windows\\System32\\cmd.exe"}; };
    o.profile_dir = t.path / "perfil";
    o.base_timeout_seconds = 1;
    o.per_page_seconds = 0;
    o.command_override = [](const fs::path&, const fs::path&, const fs::path&) {
        return std::wstring(L"C:\\Windows\\System32\\cmd.exe /c ping -n 30 127.0.0.1");
    };
    EdgePdfGenerator gen(o);
    const auto start = std::chrono::steady_clock::now();
    const PdfResult r = gen.generate(t.path / "a.html", t.path / "a.pdf", 1);
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "tiempo_agotado");
    CHECK(elapsed < 15);
}

TEST_CASE("PDF: un codigo de salida distinto de cero o un archivo no valido dan fallo_edge") {
    Dir t("wc_pdf_fail");
    EdgePdfGenerator::Options o;
    o.find_edge = [] { return std::optional<fs::path>{"C:\\Windows\\System32\\cmd.exe"}; };
    o.profile_dir = t.path / "perfil";
    o.command_override = [](const fs::path&, const fs::path&, const fs::path&) {
        return std::wstring(L"C:\\Windows\\System32\\cmd.exe /c exit 3");
    };
    EdgePdfGenerator gen(o);
    PdfResult r = gen.generate(t.path / "a.html", t.path / "a.pdf", 1);
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "fallo_edge");
    CHECK(r.message.find("3") != std::string::npos);

    // sale con 0 pero sin dejar un PDF
    o.command_override = [](const fs::path&, const fs::path&, const fs::path&) {
        return std::wstring(L"C:\\Windows\\System32\\cmd.exe /c exit 0");
    };
    EdgePdfGenerator gen2(o);
    r = gen2.generate(t.path / "a.html", t.path / "a.pdf", 1);
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "fallo_edge");

    // sale con 0 y deja un archivo que no empieza por %PDF
    write_text_file(t.path / "falso.pdf", "no soy un pdf");
    o.command_override = [&](const fs::path&, const fs::path&, const fs::path&) {
        return std::wstring(L"C:\\Windows\\System32\\cmd.exe /c exit 0");
    };
    r = EdgePdfGenerator(o).generate(t.path / "a.html", t.path / "falso.pdf", 1);
    CHECK_FALSE(r.ok);  // el PDF previo se borra antes de intentarlo, y no se crea otro
}

TEST_CASE("PDF: Edge genera un PDF de 1080x1350 (810 x 1012.5 pt) con el texto seleccionable") {
    if (!find_edge()) {
        MESSAGE("Edge no esta instalado: prueba omitida");
        return;
    }
    Dir t("wc_pdf_real");
    write_text_file(t.path / "pagina con espacios.html", kSimplePage);
    EdgePdfGenerator::Options o;
    o.profile_dir = t.path / "perfil";
    EdgePdfGenerator gen(o);
    const PdfResult r = gen.generate(t.path / "pagina con espacios.html", t.path / "salida.pdf", 1);
    INFO(r.reason << ": " << r.message);
    REQUIRE(r.ok);
    CHECK(slurp(t.path / "salida.pdf").substr(0, 4) == "%PDF");
    double w = 0, h = 0;
    REQUIRE(media_box(t.path / "salida.pdf", w, h));
    CHECK(w == doctest::Approx(810.0).epsilon(0.002));
    CHECK(h == doctest::Approx(1012.5).epsilon(0.002));  // Edge redondea a pixeles de dispositivo: ~1013
}
