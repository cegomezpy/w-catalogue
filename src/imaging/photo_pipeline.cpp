#include "imaging/photo_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imaging/exif.hpp"
#include "imaging/image.hpp"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

namespace wcatalog {

namespace {

const char* const kUnsupported = "Formato no admitido: solo se aceptan fotos JPEG y PNG.";

enum class Kind { Jpeg, Png, Other };

Kind sniff(const std::uint8_t* d, std::size_t n) {
    static const std::uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return Kind::Jpeg;
    if (n >= 8 && std::memcmp(d, png, 8) == 0) return Kind::Png;
    return Kind::Other;
}

// stb rellena con ceros un JPEG cortado y devuelve una imagen "válida" a medias; se exige el
// marcador EOI después del inicio de los datos (SOS). Los EOI de miniaturas EXIF quedan antes.
bool jpeg_is_complete(const std::uint8_t* d, std::size_t n) {
    std::size_t pos = 2;
    while (pos + 1 < n) {
        if (d[pos] != 0xFF) return false;
        const std::uint8_t marker = d[pos + 1];
        if (marker == 0xFF) {  // relleno
            ++pos;
            continue;
        }
        pos += 2;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;  // sin longitud
        if (marker == 0xD9) return true;
        if (pos + 2 > n) return false;
        const std::size_t len = std::size_t(d[pos]) << 8 | d[pos + 1];
        if (len < 2 || pos + len > n) return false;
        pos += len;
        if (marker == 0xDA) {
            // datos entrópicos: FF00 es relleno y FFD0..D7 reinicios; otro FFxx es el siguiente marcador
            while (pos + 1 < n &&
                   !(d[pos] == 0xFF && d[pos + 1] != 0x00 && d[pos + 1] != 0xFF && !(d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7))) {
                ++pos;
            }
            if (pos + 1 >= n) return false;
        }
    }
    return false;
}

std::vector<std::uint8_t> encode_jpeg(const Image& im, int quality) {
    std::vector<std::uint8_t> out;
    auto sink = [](void* ctx, void* data, int n) {
        auto* b = static_cast<std::vector<std::uint8_t>*>(ctx);
        b->insert(b->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + n);
    };
    if (!stbi_write_jpg_to_func(sink, &out, im.width, im.height, im.channels, im.pixels.data(), quality) ||
        out.empty()) {
        throw std::runtime_error("No se pudo codificar la imagen a JPEG");
    }
    return out;
}

Image resized(const Image& src, int max_side) {
    int w = 0, h = 0;
    fit_within(src.width, src.height, max_side, w, h);
    if (w == src.width && h == src.height) return src;

    Image dst;
    dst.width = w;
    dst.height = h;
    dst.channels = 3;
    dst.pixels.resize(std::size_t(w) * h * 3);
    if (!stbir_resize_uint8_linear(src.pixels.data(), src.width, src.height, src.width * 3, dst.pixels.data(), w, h,
                                   w * 3, STBIR_RGB)) {
        throw std::runtime_error("No se pudo redimensionar la imagen");
    }
    return dst;
}

}  // namespace

void fit_within(int width, int height, int max_side, int& out_w, int& out_h) {
    const int longest = std::max(width, height);
    if (longest <= max_side) {
        out_w = width;
        out_h = height;
        return;
    }
    const double scale = double(max_side) / longest;
    out_w = std::max(1, static_cast<int>(std::lround(width * scale)));
    out_h = std::max(1, static_cast<int>(std::lround(height * scale)));
    if (width >= height) out_w = max_side; else out_h = max_side;
}

ProcessedPhoto process_photo(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) throw PhotoRejected("El archivo está vacío.");
    if (size > kPhotoMaxBytes) throw PhotoRejected("La foto supera el máximo de 25 MB.");

    const Kind kind = sniff(data, size);
    if (kind == Kind::Other) throw PhotoRejected(kUnsupported);
    if (kind == Kind::Jpeg && !jpeg_is_complete(data, size)) {
        throw PhotoRejected("El archivo está dañado o incompleto y no se pudo leer.");
    }

    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &comp) || w <= 0 || h <= 0) {
        throw PhotoRejected("El archivo está dañado o incompleto y no se pudo leer.");
    }
    if (static_cast<long long>(w) * h > kPhotoMaxPixels) {
        throw PhotoRejected("La foto supera el máximo de 60 megapíxeles.");
    }

    const int want = (kind == Kind::Png) ? 4 : 3;
    int dw = 0, dh = 0, dc = 0;
    std::uint8_t* raw = stbi_load_from_memory(data, static_cast<int>(size), &dw, &dh, &dc, want);
    if (!raw) throw PhotoRejected("El archivo está dañado o incompleto y no se pudo leer.");

    Image im;
    im.width = dw;
    im.height = dh;
    im.channels = 3;
    im.pixels.resize(std::size_t(dw) * dh * 3);
    if (want == 4) {
        // compone sobre blanco: c' = (c*a + 255*(255-a)) / 255
        for (std::size_t i = 0, n = std::size_t(dw) * dh; i < n; ++i) {
            const unsigned a = raw[i * 4 + 3];
            for (int k = 0; k < 3; ++k) {
                im.pixels[i * 3 + k] = static_cast<std::uint8_t>((raw[i * 4 + k] * a + 255u * (255u - a) + 127u) / 255u);
            }
        }
    } else {
        std::memcpy(im.pixels.data(), raw, im.pixels.size());
    }
    stbi_image_free(raw);

    if (kind == Kind::Jpeg) {
        const int orientation = read_jpeg_orientation(data, size);
        if (orientation != 1) im = apply_orientation(im, orientation);
    }

    const Image opt = resized(im, kPhotoOptSide);
    ProcessedPhoto out;
    out.original_ext = (kind == Kind::Png) ? "png" : "jpg";
    out.width = opt.width;
    out.height = opt.height;
    out.opt_jpeg = encode_jpeg(opt, kPhotoOptQuality);
    out.min_jpeg = encode_jpeg(resized(opt, kPhotoMinSide), kPhotoMinQuality);
    return out;
}

}  // namespace wcatalog
