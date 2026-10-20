#include "imaging/exif.hpp"

#include <cstring>

namespace wcatalog {

namespace {

struct Reader {
    const std::uint8_t* p;
    std::size_t size;
    bool big;  // orden de bytes TIFF

    bool has(std::size_t off, std::size_t n) const { return off <= size && n <= size - off; }
    unsigned u16(std::size_t off) const {
        return big ? (p[off] << 8 | p[off + 1]) : (p[off + 1] << 8 | p[off]);
    }
    std::uint32_t u32(std::size_t off) const {
        return big ? (std::uint32_t(p[off]) << 24 | p[off + 1] << 16 | p[off + 2] << 8 | p[off + 3])
                   : (std::uint32_t(p[off + 3]) << 24 | p[off + 2] << 16 | p[off + 1] << 8 | p[off]);
    }
};

// `tiff` apunta a la cabecera TIFF dentro del segmento APP1 (tras "Exif\0\0").
int orientation_from_tiff(const std::uint8_t* tiff, std::size_t size) {
    if (size < 8) return 1;
    Reader r{tiff, size, false};
    if (tiff[0] == 'M' && tiff[1] == 'M') {
        r.big = true;
    } else if (!(tiff[0] == 'I' && tiff[1] == 'I')) {
        return 1;
    }
    if (r.u16(2) != 42) return 1;

    const std::uint32_t ifd = r.u32(4);
    if (!r.has(ifd, 2)) return 1;
    const unsigned count = r.u16(ifd);
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t entry = std::size_t(ifd) + 2 + std::size_t(i) * 12;
        if (!r.has(entry, 12)) return 1;
        if (r.u16(entry) != 0x0112) continue;
        if (r.u16(entry + 2) != 3 || r.u32(entry + 4) < 1) return 1;  // SHORT, al menos un valor
        const unsigned value = r.u16(entry + 8);
        return (value >= 1 && value <= 8) ? static_cast<int>(value) : 1;
    }
    return 1;
}

}  // namespace

int read_jpeg_orientation(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return 1;

    std::size_t pos = 2;
    while (pos + 4 <= size) {
        if (data[pos] != 0xFF) return 1;
        while (pos < size && data[pos] == 0xFF) ++pos;  // relleno
        if (pos >= size) return 1;
        const std::uint8_t marker = data[pos++];

        // sin longitud: SOI, TEM, RSTn
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;
        if (marker == 0xD9 || marker == 0xDA) return 1;  // fin de imagen o inicio de datos: ya no hay EXIF

        if (pos + 2 > size) return 1;
        const std::size_t len = std::size_t(data[pos]) << 8 | data[pos + 1];
        if (len < 2 || pos + len > size) return 1;

        if (marker == 0xE1 && len >= 2 + 6 && std::memcmp(data + pos + 2, "Exif\0\0", 6) == 0) {
            return orientation_from_tiff(data + pos + 8, len - 8);
        }
        pos += len;
    }
    return 1;
}

Image apply_orientation(const Image& src, int orientation) {
    if (src.empty() || orientation < 2 || orientation > 8) return src;

    const int w = src.width, h = src.height, c = src.channels;
    const bool swap = orientation >= 5;
    Image dst;
    dst.width = swap ? h : w;
    dst.height = swap ? w : h;
    dst.channels = c;
    dst.pixels.resize(src.pixels.size());

    for (int dy = 0; dy < dst.height; ++dy) {
        for (int dx = 0; dx < dst.width; ++dx) {
            int sx = dx, sy = dy;
            switch (orientation) {
                case 2: sx = w - 1 - dx; break;
                case 3: sx = w - 1 - dx; sy = h - 1 - dy; break;
                case 4: sy = h - 1 - dy; break;
                case 5: sx = dy; sy = dx; break;
                case 6: sx = dy; sy = h - 1 - dx; break;
                case 7: sx = w - 1 - dy; sy = h - 1 - dx; break;
                case 8: sx = w - 1 - dy; sy = dx; break;
            }
            std::memcpy(&dst.pixels[(std::size_t(dy) * dst.width + dx) * c],
                        &src.pixels[(std::size_t(sy) * w + sx) * c], std::size_t(c));
        }
    }
    return dst;
}

}  // namespace wcatalog
