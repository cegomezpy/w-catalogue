#pragma once

// Fixtures de fotos para los tests: JPEG/PNG generados en memoria (no hay binarios en el repo)
// y JPEG con el tag de orientación EXIF inyectado.

#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "imaging/image.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

namespace photo_test {

using Bytes = std::vector<std::uint8_t>;

struct Dir {
    std::filesystem::path path;
    explicit Dir(const char* name) {
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~Dir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

inline void append_to(void* ctx, void* data, int n) {
    auto* b = static_cast<Bytes*>(ctx);
    b->insert(b->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + n);
}

// JPEG real de 16x8 (cuatro colores) codificado con stb, sin EXIF.
inline Bytes plain_jpeg() {
    Bytes px(16 * 8 * 3);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            std::uint8_t* p = &px[(y * 16 + x) * 3];
            p[0] = x < 8 ? 220 : 20;
            p[1] = y < 4 ? 220 : 20;
            p[2] = 128;
        }
    }
    Bytes out;
    REQUIRE(stbi_write_jpg_to_func(append_to, &out, 16, 8, 3, px.data(), 90) != 0);
    return out;
}

// Inserta un APP1 Exif con el tag de orientación justo tras el SOI.
inline Bytes with_exif(const Bytes& jpeg, int orientation, bool big_endian, std::uint16_t type = 3) {
    auto put16 = [&](Bytes& b, unsigned v) {
        if (big_endian) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
        else { b.push_back(v & 0xFF); b.push_back(v >> 8); }
    };
    auto put32 = [&](Bytes& b, unsigned v) {
        put16(b, big_endian ? v >> 16 : v & 0xFFFF);
        put16(b, big_endian ? v & 0xFFFF : v >> 16);
    };
    Bytes tiff;
    tiff.push_back(big_endian ? 'M' : 'I');
    tiff.push_back(big_endian ? 'M' : 'I');
    put16(tiff, 42);
    put32(tiff, 8);
    put16(tiff, 2);  // dos entradas: una previa y la orientación
    put16(tiff, 0x010F); put16(tiff, 2); put32(tiff, 1); put32(tiff, 0);  // Make (ignorada)
    put16(tiff, 0x0112); put16(tiff, type); put32(tiff, 1);
    put16(tiff, orientation); put16(tiff, 0);
    put32(tiff, 0);  // sin IFD siguiente

    Bytes seg = {0xFF, 0xE1};
    const unsigned len = 2 + 6 + static_cast<unsigned>(tiff.size());
    seg.push_back(len >> 8); seg.push_back(len & 0xFF);
    for (char ch : std::string("Exif\0\0", 6)) seg.push_back(static_cast<std::uint8_t>(ch));
    seg.insert(seg.end(), tiff.begin(), tiff.end());

    Bytes out(jpeg.begin(), jpeg.begin() + 2);  // SOI
    out.insert(out.end(), seg.begin(), seg.end());
    out.insert(out.end(), jpeg.begin() + 2, jpeg.end());
    return out;
}

inline wcatalog::Image make_3x2() {
    wcatalog::Image im;
    im.width = 3; im.height = 2; im.channels = 1;
    im.pixels = {'a', 'b', 'c', 'd', 'e', 'f'};
    return im;
}

inline std::string text_of(const wcatalog::Image& im) { return std::string(im.pixels.begin(), im.pixels.end()); }

// Degradado RGB de w x h codificado como JPEG.
inline Bytes encode_jpeg_rgb(int w, int h, int quality = 85) {
    Bytes px(std::size_t(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &px[(std::size_t(y) * w + x) * 3];
            p[0] = static_cast<std::uint8_t>(x * 255 / w);
            p[1] = static_cast<std::uint8_t>(y * 255 / h);
            p[2] = 90;
        }
    }
    Bytes out;
    REQUIRE(stbi_write_jpg_to_func(append_to, &out, w, h, 3, px.data(), quality) != 0);
    return out;
}

inline Bytes encode_png_rgba(int w, int h, const std::vector<std::uint8_t>& rgba) {
    Bytes out;
    REQUIRE(stbi_write_png_to_func(append_to, &out, w, h, 4, rgba.data(), w * 4) != 0);
    return out;
}

struct Dims { int w = 0, h = 0; };

inline Dims dims_of(const Bytes& jpeg) {
    Dims d;
    int c = 0;
    REQUIRE(stbi_info_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &d.w, &d.h, &c) != 0);
    return d;
}

// Píxel RGB de un JPEG generado.
inline void pixel_at(const Bytes& jpeg, int x, int y, int rgb[3]) {
    int w, h, c;
    std::uint8_t* px = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &c, 3);
    REQUIRE(px != nullptr);
    for (int k = 0; k < 3; ++k) rgb[k] = px[(std::size_t(y) * w + x) * 3 + k];
    stbi_image_free(px);
}

}  // namespace photo_test
