#include "doctest.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "db/migrations.hpp"
#include "imaging/exif.hpp"
#include "photo_fixtures.hpp"
#include "imaging/photo_pipeline.hpp"
#include "stb_image.h"

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace photo_test;

TEST_CASE("Fotos: la migracion se aplica sobre una base con casas y la cascada borra sus fotos") {
    Dir tmp("wcat_photos_migration");
    std::vector<Migration> upto3(app_migrations().begin(), app_migrations().begin() + 3);
    {
        Database db(tmp.path / "c.db");
        migrate(db, upto3, tmp.path / "backups");
        db.exec("INSERT INTO casa (titulo, creado, actualizado) VALUES ('A', 'x', 'x'), ('B', 'x', 'x');");
        REQUIRE(db.user_version() == 3);
    }

    Database db(tmp.path / "c.db");
    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() == latest_version(app_migrations()));
    CHECK(db.user_version() >= 4);

    db.exec("PRAGMA foreign_keys = ON;");
    db.exec("INSERT INTO foto (casa_id, orden, formato_original, ancho, alto, bytes_original) "
            "VALUES (1, 1, 'jpg', 10, 10, 100), (1, 2, 'png', 10, 10, 100), (2, 1, 'jpg', 10, 10, 100);");

    auto count = [&](const char* sql) {
        Statement st = db.prepare(sql);
        REQUIRE(st.step());
        return st.column_int(0);
    };
    CHECK(count("SELECT count(*) FROM foto") == 3);
    CHECK(count("SELECT count(*) FROM casa") == 2);  // las casas previas siguen intactas

    db.exec("DELETE FROM casa WHERE id = 1;");
    CHECK(count("SELECT count(*) FROM foto") == 1);

    CHECK_THROWS(db.exec("INSERT INTO foto (casa_id, orden, formato_original, ancho, alto, bytes_original) "
                         "VALUES (999, 1, 'jpg', 10, 10, 100);"));  // casa inexistente
    CHECK_THROWS(db.exec("INSERT INTO foto (casa_id, orden, formato_original, ancho, alto, bytes_original, pie) "
                         "VALUES (2, 2, 'jpg', 10, 10, 100, '" + std::string(121, 'x') + "');"));
}

// ---------- EXIF: orientación ----------

TEST_CASE("EXIF: orientaciones 1 a 8 se leen en ambos ordenes de bytes") {
    const Bytes base = plain_jpeg();
    CHECK(read_jpeg_orientation(base.data(), base.size()) == 1);  // sin EXIF
    for (int o = 1; o <= 8; ++o) {
        for (bool big : {false, true}) {
            const Bytes j = with_exif(base, o, big);
            INFO("orientacion " << o << (big ? " MM" : " II"));
            CHECK(read_jpeg_orientation(j.data(), j.size()) == o);
        }
    }
}

TEST_CASE("EXIF: datos invalidos o truncados dan orientacion normal") {
    const Bytes base = plain_jpeg();
    const Bytes j = with_exif(base, 6, false);

    CHECK(read_jpeg_orientation(nullptr, 0) == 1);
    CHECK(read_jpeg_orientation(j.data(), 3) == 1);
    CHECK(read_jpeg_orientation(with_exif(base, 0, false).data(), j.size()) == 1);   // fuera de rango
    CHECK(read_jpeg_orientation(with_exif(base, 9, true).data(), j.size()) == 1);
    const Bytes wrong_type = with_exif(base, 6, false, 4);                           // LONG, no SHORT
    CHECK(read_jpeg_orientation(wrong_type.data(), wrong_type.size()) == 1);

    Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(read_jpeg_orientation(png.data(), png.size()) == 1);

    // cualquier recorte del archivo (incluso en mitad del EXIF) debe ser seguro
    for (std::size_t n = 0; n < 60 && n < j.size(); ++n) {
        const int o = read_jpeg_orientation(j.data(), n);
        CHECK((o == 1 || o == 6));
    }
}

TEST_CASE("EXIF: las 8 transformaciones sobre los pixeles") {
    const Image src = make_3x2();  // a b c / d e f
    struct Case { int o; int w, h; const char* px; };
    const Case cases[] = {
        {1, 3, 2, "abcdef"}, {2, 3, 2, "cbafed"}, {3, 3, 2, "fedcba"}, {4, 3, 2, "defabc"},
        {5, 2, 3, "adbecf"}, {6, 2, 3, "daebfc"}, {7, 2, 3, "fcebda"}, {8, 2, 3, "cfbead"},
    };
    for (const Case& c : cases) {
        INFO("orientacion " << c.o);
        const Image out = apply_orientation(src, c.o);
        CHECK(out.width == c.w);
        CHECK(out.height == c.h);
        CHECK(text_of(out) == c.px);
    }
    CHECK(text_of(apply_orientation(src, 0)) == "abcdef");
    CHECK(text_of(apply_orientation(src, 9)) == "abcdef");
}

TEST_CASE("EXIF: la transformacion conserva todos los canales") {
    Image im;
    im.width = 2; im.height = 1; im.channels = 3;
    im.pixels = {1, 2, 3, 4, 5, 6};
    const Image out = apply_orientation(im, 6);  // 2x1 -> 1x2
    CHECK(out.width == 1);
    CHECK(out.height == 2);
    CHECK(out.pixels == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6});
    const Image mirrored = apply_orientation(im, 2);
    CHECK(mirrored.pixels == std::vector<std::uint8_t>{4, 5, 6, 1, 2, 3});
}

// ---------- Pipeline de fotos ----------

TEST_CASE("Pipeline: 4000x3000 -> 2000x1500 y miniatura 400x300") {
    const Bytes src = encode_jpeg_rgb(4000, 3000);
    const ProcessedPhoto p = process_photo(src.data(), src.size());
    CHECK(p.original_ext == "jpg");
    CHECK(p.width == 2000);
    CHECK(p.height == 1500);
    const Dims opt = dims_of(p.opt_jpeg);
    const Dims min = dims_of(p.min_jpeg);
    CHECK(opt.w == 2000);
    CHECK(opt.h == 1500);
    CHECK(min.w == 400);
    CHECK(min.h == 300);
    CHECK(p.min_jpeg.size() < p.opt_jpeg.size());
}

TEST_CASE("Pipeline: 800x600 no se amplia y la miniatura baja a 400x300") {
    const Bytes src = encode_jpeg_rgb(800, 600);
    const ProcessedPhoto p = process_photo(src.data(), src.size());
    CHECK(p.width == 800);
    CHECK(p.height == 600);
    CHECK(dims_of(p.opt_jpeg).w == 800);
    CHECK(dims_of(p.opt_jpeg).h == 600);
    CHECK(dims_of(p.min_jpeg).w == 400);
    CHECK(dims_of(p.min_jpeg).h == 300);

    const Bytes tiny = encode_jpeg_rgb(120, 80);
    const ProcessedPhoto t = process_photo(tiny.data(), tiny.size());
    CHECK(dims_of(t.opt_jpeg).w == 120);
    CHECK(dims_of(t.min_jpeg).w == 120);
    CHECK(dims_of(t.min_jpeg).h == 80);
}

TEST_CASE("Pipeline: foto vertical de telefono (EXIF 6) sale vertical") {
    const Bytes src = with_exif(plain_jpeg(), 6, false);  // 16x8 guardada de lado
    const ProcessedPhoto p = process_photo(src.data(), src.size());
    CHECK(p.width == 8);
    CHECK(p.height == 16);
    CHECK(dims_of(p.opt_jpeg).w == 8);
    CHECK(dims_of(p.opt_jpeg).h == 16);
    CHECK(dims_of(p.min_jpeg).h == 16);

    // al girar 90 grados en sentido horario, el borde superior original (verde alto) queda a la derecha
    int top_right[3], bottom_left[3];
    pixel_at(p.opt_jpeg, 7, 1, top_right);
    pixel_at(p.opt_jpeg, 0, 14, bottom_left);
    CHECK(top_right[1] > 150);    // y < 4 en el original: verde alto, arriba-derecha tras girar
    CHECK(bottom_left[1] < 100);
}

TEST_CASE("Pipeline: PNG con transparencia se compone sobre blanco") {
    const int w = 8, h = 8;
    std::vector<std::uint8_t> rgba(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        const bool left = (i % w) < w / 2;
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 0; rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = left ? 0 : 255;  // izquierda transparente (rojo oculto), derecha opaca
    }
    const Bytes png = encode_png_rgba(w, h, rgba);
    const ProcessedPhoto p = process_photo(png.data(), png.size());
    CHECK(p.original_ext == "png");
    int transparent[3], opaque[3];
    pixel_at(p.opt_jpeg, 1, 4, transparent);
    pixel_at(p.opt_jpeg, 6, 4, opaque);
    for (int k = 0; k < 3; ++k) CHECK(transparent[k] > 240);  // blanco
    CHECK(opaque[0] > 200);
    CHECK(opaque[1] < 60);
}

TEST_CASE("Pipeline: archivos corruptos o no admitidos se rechazan") {
    const Bytes good = encode_jpeg_rgb(640, 480);

    Bytes truncated(good.begin(), good.begin() + good.size() / 2);
    CHECK_THROWS_AS(process_photo(truncated.data(), truncated.size()), PhotoRejected);

    Bytes no_eoi(good.begin(), good.end() - 2);
    CHECK_THROWS_AS(process_photo(no_eoi.data(), no_eoi.size()), PhotoRejected);

    Bytes garbage = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'x', 'y', 'z'};
    CHECK_THROWS_AS(process_photo(garbage.data(), garbage.size()), PhotoRejected);

    const std::string webp = "RIFF\x24\0\0\0WEBPVP8 ";
    CHECK_THROWS_AS(process_photo(reinterpret_cast<const std::uint8_t*>(webp.data()), webp.size()), PhotoRejected);

    const std::string text = "esto no es una imagen";
    CHECK_THROWS_AS(process_photo(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), PhotoRejected);

    CHECK_THROWS_AS(process_photo(nullptr, 0), PhotoRejected);

    Bytes png_cut;
    {
        std::vector<std::uint8_t> rgba(32 * 32 * 4, 200);
        const Bytes png = encode_png_rgba(32, 32, rgba);
        png_cut.assign(png.begin(), png.begin() + png.size() / 2);
    }
    CHECK_THROWS_AS(process_photo(png_cut.data(), png_cut.size()), PhotoRejected);
}

TEST_CASE("Pipeline: limites de tamano y de megapixeles") {
    Bytes huge(kPhotoMaxBytes + 1, 0);
    huge[0] = 0xFF; huge[1] = 0xD8; huge[2] = 0xFF;
    CHECK_THROWS_WITH_AS(process_photo(huge.data(), huge.size()), doctest::Contains("25 MB"), PhotoRejected);

    // JPEG válido de 8001x8001 (64 MP): la cabecera basta para rechazarlo sin decodificar
    Bytes big = encode_jpeg_rgb(64, 64);
    for (std::size_t i = 0; i + 8 < big.size(); ++i) {
        if (big[i] == 0xFF && big[i + 1] == 0xC0) {  // SOF0: longitud(2) precisión(1) alto(2) ancho(2)
            big[i + 5] = 8001 >> 8; big[i + 6] = 8001 & 0xFF;
            big[i + 7] = 8001 >> 8; big[i + 8] = 8001 & 0xFF;
            break;
        }
    }
    CHECK_THROWS_WITH_AS(process_photo(big.data(), big.size()), doctest::Contains("60 megap"), PhotoRejected);
}

TEST_CASE("Pipeline: fit_within no amplia y conserva la proporcion") {
    int w, h;
    fit_within(4000, 3000, 2000, w, h); CHECK((w == 2000 && h == 1500));
    fit_within(3000, 4000, 2000, w, h); CHECK((w == 1500 && h == 2000));
    fit_within(800, 600, 2000, w, h);   CHECK((w == 800 && h == 600));
    fit_within(10000, 10, 2000, w, h);  CHECK((w == 2000 && h == 2));
    fit_within(100000, 1, 2000, w, h);  CHECK((w == 2000 && h == 1));
}
