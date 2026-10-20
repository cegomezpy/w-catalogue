#include "doctest.h"

#include <httplib.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "db/migrations.hpp"
#include "houses/feature_service.hpp"
#include "houses/house_service.hpp"
#include "http/routes_houses.hpp"
#include "http/routes_photos.hpp"
#include "http/server.hpp"
#include "photo_fixtures.hpp"
#include "photos/photo_service.hpp"
#include "settings/settings_service.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace photo_test;

namespace {

struct PhotoEnv {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<SettingsService> settings;
    std::unique_ptr<FeatureService> features;
    std::unique_ptr<HouseService> houses;
    std::unique_ptr<PhotoService> photos;

    explicit PhotoEnv(const char* name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir / "fotos");
        fs::create_directories(dir / "tmp");
        db = open_app_database(dir / "c.db", dir / "backups");
        settings = std::make_unique<SettingsService>(*db);
        features = std::make_unique<FeatureService>(*db);
        houses = std::make_unique<HouseService>(*db, *settings, *features);
        photos = std::make_unique<PhotoService>(*db, PhotoPaths{dir / "fotos", dir / "tmp"});
        houses->set_photo_service(photos.get());
    }
    ~PhotoEnv() {
        photos.reset();
        houses.reset();
        features.reset();
        settings.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::int64_t new_house(const char* title = "Casa") {
        return houses->create(Json{{"titulo", title}})["id"].get<std::int64_t>();
    }
    Json add(std::int64_t house, const Bytes& jpeg) { return photos->upload(house, jpeg.data(), jpeg.size()); }
    std::size_t files_in(const fs::path& p) {
        std::error_code ec;
        if (!fs::is_directory(p, ec)) return 0;
        return static_cast<std::size_t>(std::distance(fs::directory_iterator(p), fs::directory_iterator()));
    }
    std::vector<std::int64_t> order_of(std::int64_t house) {
        std::vector<std::int64_t> ids;
        for (const Json& f : photos->list(house)) ids.push_back(f["id"].get<std::int64_t>());
        return ids;
    }
};

// std::filesystem no puede cambiar la fecha de una carpeta con MinGW: se hace con la API de Windows.
void age_dir(const fs::path& dir, int hours) {
    HANDLE h = CreateFileW(dir.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER u;
    u.LowPart = now.dwLowDateTime;
    u.HighPart = now.dwHighDateTime;
    u.QuadPart -= static_cast<ULONGLONG>(hours) * 3600ULL * 10000000ULL;
    FILETIME then{u.LowPart, u.HighPart};
    REQUIRE(SetFileTime(h, nullptr, nullptr, &then));
    CloseHandle(h);
}

bool mentions(const Json& url, std::int64_t photo_id) {
    return url.is_string() && url.get<std::string>().find(std::to_string(photo_id) + "_min") != std::string::npos;
}

}  // namespace

TEST_CASE("Fotos: subida de varias fotos en orden, con archivos y portada") {
    PhotoEnv e("wcat-photos-upload");
    const std::int64_t h = e.new_house();
    const Bytes jpeg = encode_jpeg_rgb(640, 480);
    for (int i = 0; i < 5; ++i) e.add(h, jpeg);

    const Json list = e.photos->list(h);
    REQUIRE(list.size() == 5);
    for (int i = 0; i < 5; ++i) CHECK(list[i]["orden"] == i + 1);
    CHECK(list[0]["ancho"] == 640);
    CHECK(list[0]["alto"] == 480);
    CHECK(list[0]["pie"] == "");

    const std::string id0 = std::to_string(list[0]["id"].get<std::int64_t>());
    const fs::path folder = e.dir / "fotos" / std::to_string(h);
    CHECK(fs::exists(folder / (id0 + "_orig.jpg")));
    CHECK(fs::exists(folder / (id0 + "_opt.jpg")));
    CHECK(fs::exists(folder / (id0 + "_min.jpg")));
    CHECK(e.files_in(folder) == 15);
    CHECK(list[0]["url_min"] == "/media/fotos/" + std::to_string(h) + "/" + id0 + "_min.jpg");

    // detalle y lista de casas
    CHECK(e.houses->get(h)["fotos"].size() == 5);
    CHECK(e.houses->list()[0]["portada_min"] == list[0]["url_min"]);
    CHECK(e.files_in(e.dir / "tmp") == 0);
}

TEST_CASE("Fotos: casa inexistente y casas sin fotos") {
    PhotoEnv e("wcat-photos-nohouse");
    const Bytes jpeg = encode_jpeg_rgb(64, 64);
    CHECK_THROWS_AS(e.photos->upload(999, jpeg.data(), jpeg.size()), NotFoundError);

    const std::int64_t h = e.new_house();
    CHECK(e.houses->get(h)["fotos"].empty());
    CHECK(e.houses->list()[0]["portada_min"].is_null());
}

TEST_CASE("Fotos: un fallo no deja ni registro ni archivos") {
    PhotoEnv e("wcat-photos-atomic");
    const std::int64_t h = e.new_house();
    const Bytes good = encode_jpeg_rgb(640, 480);

    // JPEG cortado: se rechaza antes de tocar nada
    const Bytes cut(good.begin(), good.begin() + good.size() / 2);
    CHECK_THROWS_AS(e.add(h, cut), PhotoRejected);
    CHECK(e.photos->list(h).empty());
    CHECK(e.files_in(e.dir / "tmp") == 0);
    CHECK(e.files_in(e.dir / "fotos") == 0);

    // fallo ya dentro de la transaccion (la carpeta de la casa no se puede crear): se revierte el registro
    { std::ofstream(e.dir / "fotos" / std::to_string(h)) << "estorbo"; }
    CHECK_THROWS(e.add(h, good));
    CHECK(e.photos->list(h).empty());
    CHECK(e.files_in(e.dir / "tmp") == 0);
    fs::remove(e.dir / "fotos" / std::to_string(h));

    // y tras el fallo todo sigue funcionando
    CHECK(e.add(h, good)["orden"] == 1);
    CHECK(e.files_in(e.dir / "fotos" / std::to_string(h)) == 3);
}

TEST_CASE("Fotos: limite de 30 por casa") {
    PhotoEnv e("wcat-photos-limit");
    const std::int64_t h = e.new_house();
    const Bytes jpeg = encode_jpeg_rgb(48, 48);
    for (int i = 0; i < kMaxPhotosPerHouse; ++i) e.add(h, jpeg);
    CHECK_THROWS_WITH_AS(e.add(h, jpeg), doctest::Contains("30"), ConflictError);
    CHECK(e.photos->list(h).size() == 30);
    CHECK(e.files_in(e.dir / "fotos" / std::to_string(h)) == 90);

    // otra casa no se ve afectada
    CHECK(e.add(e.new_house("Otra"), jpeg)["orden"] == 1);
}

TEST_CASE("Fotos: limpieza de temporales viejos al arrancar") {
    PhotoEnv e("wcat-photos-tmp");
    const fs::path old_dir = e.dir / "tmp" / "foto-viejo";
    const fs::path fresh_dir = e.dir / "tmp" / "foto-reciente";
    const fs::path other = e.dir / "tmp" / "otra-cosa";
    for (const auto& d : {old_dir, fresh_dir, other}) {
        fs::create_directories(d);
        std::ofstream(d / "x.bin") << "x";
    }
    age_dir(old_dir, 3);
    age_dir(other, 3);

    e.photos->cleanup_tmp();
    CHECK_FALSE(fs::exists(old_dir));
    CHECK(fs::exists(fresh_dir));
    CHECK(fs::exists(other));  // solo se tocan las carpetas de fotos
}

TEST_CASE("Fotos: reordenar valida el conjunto de ids y la portada sigue al primero") {
    PhotoEnv e("wcat-photos-reorder");
    const std::int64_t h = e.new_house();
    const Bytes jpeg = encode_jpeg_rgb(64, 64);
    for (int i = 0; i < 4; ++i) e.add(h, jpeg);
    const auto ids = e.order_of(h);  // a b c d

    // arrastrar la ultima a la segunda posicion
    e.photos->reorder(h, Json{ids[0], ids[3], ids[1], ids[2]});
    CHECK(e.order_of(h) == std::vector<std::int64_t>{ids[0], ids[3], ids[1], ids[2]});

    // la tercera pasa a portada y las demas conservan su orden relativo
    const auto now = e.order_of(h);
    e.photos->reorder(h, Json{now[2], now[0], now[1], now[3]});
    CHECK(e.order_of(h) == std::vector<std::int64_t>{now[2], now[0], now[1], now[3]});
    CHECK(mentions(e.houses->list()[0]["portada_min"], now[2]));

    // conjuntos invalidos: nada cambia
    const auto before = e.order_of(h);
    CHECK_THROWS_AS(e.photos->reorder(h, Json{before[0], before[1], before[2]}), ValidationFailed);  // falta una
    CHECK_THROWS_AS(e.photos->reorder(h, Json{before[0], before[0], before[1], before[2]}), ValidationFailed);
    CHECK_THROWS_AS(e.photos->reorder(h, Json{before[0], before[1], before[2], before[3], 99999}), ValidationFailed);
    CHECK_THROWS_AS(e.photos->reorder(h, Json("no")), ValidationFailed);
    CHECK_THROWS_AS(e.photos->reorder(h, Json{"a", "b", "c", "d"}), ValidationFailed);
    CHECK_THROWS_AS(e.photos->reorder(12345, Json::array()), NotFoundError);
    CHECK(e.order_of(h) == before);

    // una foto de otra casa no se puede colar
    const std::int64_t h2 = e.new_house("Otra");
    const Json foreign = e.add(h2, jpeg);
    CHECK_THROWS_AS(e.photos->reorder(h, Json{before[0], before[1], before[2], foreign["id"]}), ValidationFailed);
    CHECK(e.order_of(h) == before);
}

TEST_CASE("Fotos: pie de hasta 120 caracteres") {
    PhotoEnv e("wcat-photos-caption");
    const std::int64_t h = e.new_house();
    const Bytes jpeg = encode_jpeg_rgb(64, 64);
    const std::int64_t id = e.add(h, jpeg)["id"];

    CHECK(e.photos->set_caption(id, Json{{"pie", "  Sala principal  "}})["pie"] == "Sala principal");
    CHECK(e.photos->list(h)[0]["pie"] == "Sala principal");

    std::string ok;
    for (int i = 0; i < 120; ++i) ok += "\xC3\xB1";  // 120 caracteres, 240 bytes
    CHECK(e.photos->set_caption(id, Json{{"pie", ok}})["pie"] == ok);

    const Json before = e.photos->list(h);
    try {
        e.photos->set_caption(id, Json{{"pie", std::string(150, 'x')}});
        FAIL("debia rechazar el pie de 150 caracteres");
    } catch (const ValidationFailed& v) {
        CHECK(v.fields.at("pie").find("120") != std::string::npos);
    }
    CHECK(e.photos->list(h) == before);

    CHECK_THROWS_AS(e.photos->set_caption(id, Json{{"pie", 5}}), ValidationFailed);
    CHECK_THROWS_AS(e.photos->set_caption(id, Json::object()), ValidationFailed);
    CHECK_THROWS_AS(e.photos->set_caption(777, Json{{"pie", "x"}}), NotFoundError);
    CHECK(e.photos->set_caption(id, Json{{"pie", ""}})["pie"] == "");
}

TEST_CASE("Fotos: eliminar la portada promueve la siguiente y borra los archivos") {
    PhotoEnv e("wcat-photos-remove");
    const std::int64_t h = e.new_house();
    const Bytes jpeg = encode_jpeg_rgb(64, 64);
    for (int i = 0; i < 3; ++i) e.add(h, jpeg);
    const auto ids = e.order_of(h);
    const fs::path folder = e.dir / "fotos" / std::to_string(h);

    e.photos->remove(ids[0]);
    CHECK(e.order_of(h) == std::vector<std::int64_t>{ids[1], ids[2]});
    CHECK(mentions(e.houses->list()[0]["portada_min"], ids[1]));
    CHECK(e.files_in(folder) == 6);
    CHECK_FALSE(fs::exists(folder / (std::to_string(ids[0]) + "_opt.jpg")));
    CHECK_FALSE(fs::exists(folder / (std::to_string(ids[0]) + "_orig.jpg")));
    CHECK_THROWS_AS(e.photos->remove(ids[0]), NotFoundError);

    e.photos->remove(ids[1]);
    e.photos->remove(ids[2]);
    CHECK_FALSE(fs::exists(folder));  // sin fotos no queda carpeta vacia
    CHECK(e.houses->list()[0]["portada_min"].is_null());
}

TEST_CASE("Fotos: eliminar una casa con fotos borra su carpeta") {
    PhotoEnv e("wcat-photos-housedelete");
    const std::int64_t h = e.new_house();
    const std::int64_t keep = e.new_house("Se queda");
    const Bytes jpeg = encode_jpeg_rgb(64, 64);
    for (int i = 0; i < 8; ++i) e.add(h, jpeg);
    e.add(keep, jpeg);
    const fs::path folder = e.dir / "fotos" / std::to_string(h);
    REQUIRE(e.files_in(folder) == 24);

    e.houses->remove(h);
    CHECK_FALSE(fs::exists(folder));
    CHECK(fs::exists(e.dir / "fotos" / std::to_string(keep)));
    Statement st = e.db->prepare("SELECT count(*) FROM foto");
    REQUIRE(st.step());
    CHECK(st.column_int(0) == 1);
}

TEST_CASE("Fotos: carpetas huerfanas solo se registran, no se borran") {
    PhotoEnv e("wcat-photos-orphans");
    const std::int64_t h = e.new_house();
    e.add(h, encode_jpeg_rgb(64, 64));
    fs::create_directories(e.dir / "fotos" / "4242");
    std::ofstream(e.dir / "fotos" / "4242" / "1_opt.jpg") << "x";
    fs::create_directories(e.dir / "fotos" / "abc");

    auto orphans = e.photos->log_orphan_folders();
    std::sort(orphans.begin(), orphans.end());
    CHECK(orphans == std::vector<std::string>{"4242", "abc"});
    CHECK(fs::exists(e.dir / "fotos" / "4242" / "1_opt.jpg"));
}

TEST_CASE("API fotos: subida multipart, formato no admitido, orden, pie, borrado y entrega segura") {
    PhotoEnv e("wcat-photos-api");
    fs::create_directories(e.dir / "web");
    std::ofstream(e.dir / "web" / "index.html") << "x";
    HttpServer server(e.dir / "web");
    register_house_routes(server.raw(), *e.houses);
    register_photo_routes(server.raw(), *e.photos);
    const int port = server.start();
    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(30, 0);

    const std::int64_t h = e.new_house();
    const std::string base = "/api/casas/" + std::to_string(h) + "/fotos";
    auto upload = [&](const std::string& path, const std::string& content, const std::string& name = "foto.jpg",
                      const std::string& field = "archivo") {
        httplib::UploadFormDataItems items = {{field, content, name, "application/octet-stream"}};
        return cli.Post(path, items);
    };

    // subida correcta
    const Bytes jpeg = encode_jpeg_rgb(1200, 800);
    const std::string jpeg_str(jpeg.begin(), jpeg.end());
    auto ok = upload(base, jpeg_str);
    REQUIRE(ok);
    CHECK(ok->status == 201);
    const Json photo = Json::parse(ok->body);
    CHECK(photo["orden"] == 1);
    CHECK(photo["ancho"] == 1200);
    auto second = upload(base, jpeg_str);
    REQUIRE(second);
    CHECK(second->status == 201);

    // formato no admitido, falta de campo, casa inexistente
    auto webp = upload(base, std::string("RIFF\x24\0\0\0WEBPVP8 ", 16), "foto.webp");
    REQUIRE(webp);
    CHECK(webp->status == 400);
    CHECK(Json::parse(webp->body)["error"]["message"].get<std::string>().find("JPEG y PNG") != std::string::npos);
    auto wrong_field = upload(base, jpeg_str, "foto.jpg", "otro");
    REQUIRE(wrong_field);
    CHECK(wrong_field->status == 400);
    auto no_house = upload("/api/casas/9999/fotos", jpeg_str);
    REQUIRE(no_house);
    CHECK(no_house->status == 404);
    CHECK(Json::parse(cli.Get("/api/casas/" + std::to_string(h))->body)["fotos"].size() == 2);

    // entrega con cache inmutable
    auto media = cli.Get(photo["url_min"].get<std::string>());
    REQUIRE(media);
    CHECK(media->status == 200);
    CHECK(media->get_header_value("Content-Type") == "image/jpeg");
    CHECK(media->get_header_value("Cache-Control") == "max-age=31536000, immutable");
    CHECK(dims_of(Bytes(media->body.begin(), media->body.end())).w == 400);
    CHECK(cli.Get(photo["url_opt"].get<std::string>())->status == 200);

    // intentos de salir de la carpeta, pedir el original o rutas inexistentes
    const std::string folder = "/media/fotos/" + std::to_string(h) + "/";
    const std::string pid = std::to_string(photo["id"].get<std::int64_t>());
    for (const std::string& bad :
         {folder + "../../c.db", std::string("/media/fotos/../c.db"), std::string("/media/fotos/..%2Fc.db"),
          folder + "%2e%2e%2fc.db", folder + "..\\..\\c.db", folder + pid + "_orig.jpg", folder + "9999_min.jpg",
          std::string("/media/fotos/abc/1_min.jpg")}) {
        auto r = cli.Get(bad);
        INFO("ruta " << bad);
        REQUIRE(r);
        CHECK(r->status != 200);
        CHECK(r->body.find("SQLite") == std::string::npos);
    }

    // reordenar
    const auto ids = e.order_of(h);
    auto reorder = cli.Put(base + "/orden", Json{{"ids", Json{ids[1], ids[0]}}}.dump(), "application/json");
    REQUIRE(reorder);
    CHECK(reorder->status == 200);
    CHECK(Json::parse(reorder->body)[0]["id"] == ids[1]);
    auto bad_order = cli.Put(base + "/orden", Json{{"ids", Json{ids[1]}}}.dump(), "application/json");
    REQUIRE(bad_order);
    CHECK(bad_order->status == 400);
    CHECK(Json::parse(bad_order->body)["error"]["fields"].contains("ids"));
    CHECK(cli.Put(base + "/orden", "{}", "application/json")->status == 400);

    // pie
    const std::string photo_path = "/api/fotos/" + std::to_string(ids[0]);
    auto cap = cli.Put(photo_path, Json{{"pie", "Cocina"}}.dump(), "application/json");
    REQUIRE(cap);
    CHECK(cap->status == 200);
    CHECK(Json::parse(cap->body)["pie"] == "Cocina");
    auto long_cap = cli.Put(photo_path, Json{{"pie", std::string(150, 'x')}}.dump(), "application/json");
    REQUIRE(long_cap);
    CHECK(long_cap->status == 400);
    CHECK(Json::parse(long_cap->body)["error"]["fields"].contains("pie"));
    CHECK(cli.Put("/api/fotos/99999", Json{{"pie", "x"}}.dump(), "application/json")->status == 404);

    // lista de casas con portada y borrado
    CHECK(mentions(Json::parse(cli.Get("/api/casas")->body)[0]["portada_min"], ids[1]));
    auto del = cli.Delete(photo_path);
    REQUIRE(del);
    CHECK(del->status == 200);
    CHECK(cli.Delete(photo_path)->status == 404);
    CHECK(cli.Get("/media/fotos/" + std::to_string(h) + "/" + std::to_string(ids[0]) + "_min.jpg")->status == 404);

    // al eliminar la casa por la API, su carpeta desaparece
    auto del_house = cli.Delete("/api/casas/" + std::to_string(h));
    REQUIRE(del_house);
    CHECK(del_house->status == 200);
    CHECK_FALSE(fs::exists(e.dir / "fotos" / std::to_string(h)));
    server.stop();
}
