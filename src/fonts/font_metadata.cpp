#include "fonts/font_metadata.hpp"

#include <cstdint>
#include <cstring>

#include "stb_truetype.h"

// stb_truetype NO comprueba los límites de lo que lee ("no usar con datos no confiables"), y el
// usuario importa archivos de Internet. Por eso toda la estructura se valida aquí con lecturas
// comprobadas antes de pasarle nada a stb, y los nombres y el OS/2 se leen sin stb.

namespace wcatalog {

namespace {

struct Reader {
    const unsigned char* data;
    size_t size;

    bool has(size_t off, size_t len) const { return off <= size && len <= size - off; }
    // Lecturas big-endian; el llamador ya comprobó el rango con has().
    std::uint16_t u16(size_t off) const { return static_cast<std::uint16_t>((data[off] << 8) | data[off + 1]); }
    std::uint32_t u32(size_t off) const {
        return (std::uint32_t(data[off]) << 24) | (std::uint32_t(data[off + 1]) << 16) |
               (std::uint32_t(data[off + 2]) << 8) | data[off + 3];
    }
};

struct Table {
    size_t offset = 0;
    size_t length = 0;
    bool found = false;
};

[[noreturn]] void invalid(const std::string& why) {
    throw FontError(FontError::Kind::Invalid, "El archivo no es una fuente TrueType/OpenType válida (" + why + ")");
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string utf16be_to_utf8(const Reader& r, size_t off, size_t len) {
    std::string out;
    for (size_t i = 0; i + 1 < len; i += 2) {
        std::uint32_t u = r.u16(off + i);
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < len) {
            std::uint32_t lo = r.u16(off + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, u);
    }
    return out;
}

// Cadena de la tabla `name`: Windows Unicode (preferiendo inglés) o, si falta, Macintosh Roman.
// Todas las lecturas están acotadas a la tabla.
std::string name_string(const Reader& r, const Table& name, int name_id) {
    if (!name.found || name.length < 6) return {};
    const size_t count = r.u16(name.offset + 2);
    const size_t strings = name.offset + r.u16(name.offset + 4);  // el área de cadenas empieza aquí

    std::string windows_english, windows_other, mac;
    for (size_t i = 0; i < count; ++i) {
        const size_t rec = name.offset + 6 + 12 * i;
        if (rec + 12 > name.offset + name.length) break;
        if (r.u16(rec + 6) != name_id) continue;
        const std::uint16_t platform = r.u16(rec), encoding = r.u16(rec + 2), language = r.u16(rec + 4);
        const size_t len = r.u16(rec + 8);
        const size_t off = strings + r.u16(rec + 10);
        if (off < name.offset || off + len > name.offset + name.length || !r.has(off, len)) continue;

        if (platform == 3 && (encoding == 1 || encoding == 10)) {
            std::string s = utf16be_to_utf8(r, off, len);
            if (language == 0x409 && windows_english.empty()) windows_english = s;
            else if (windows_other.empty()) windows_other = s;
        } else if (platform == 1 && encoding == 0 && mac.empty()) {
            mac.assign(reinterpret_cast<const char*>(r.data + off), len);
            for (char& c : mac) {
                if (static_cast<unsigned char>(c) >= 0x80) c = '?';  // Mac Roman fuera de ASCII: no se interpreta
            }
        }
    }
    if (!windows_english.empty()) return windows_english;
    if (!windows_other.empty()) return windows_other;
    return mac;
}

}  // namespace

FontMetadata read_font_metadata(const std::string& bytes) {
    if (bytes.empty()) throw FontError(FontError::Kind::Invalid, "El archivo está vacío");
    if (bytes.size() > kMaxFontBytes) {
        throw FontError(FontError::Kind::Invalid, "El archivo supera el máximo de 10 MB");
    }
    const Reader r{reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()};
    if (!r.has(0, 12)) invalid("archivo demasiado corto");

    FontMetadata md;
    const std::uint32_t sig = r.u32(0);
    if (sig == 0x00010000 || sig == 0x74727565 /* 'true' */) md.format = "ttf";
    else if (sig == 0x4F54544F /* 'OTTO' */) md.format = "otf";
    else invalid("se admiten .ttf y .otf");

    // directorio de tablas
    const size_t num_tables = r.u16(4);
    if (num_tables == 0 || num_tables > 128) invalid("número de tablas no válido");
    if (!r.has(12, num_tables * 16)) invalid("directorio de tablas incompleto");

    Table head, maxp, cmap, name, os2, glyf, loca, cff;
    for (size_t i = 0; i < num_tables; ++i) {
        const size_t rec = 12 + 16 * i;
        const size_t off = r.u32(rec + 8), len = r.u32(rec + 12);
        if (!r.has(off, len)) invalid("el archivo está incompleto o dañado");
        Table t{off, len, true};
        const char* tag = reinterpret_cast<const char*>(r.data + rec);
        if (!std::memcmp(tag, "head", 4)) head = t;
        else if (!std::memcmp(tag, "maxp", 4)) maxp = t;
        else if (!std::memcmp(tag, "cmap", 4)) cmap = t;
        else if (!std::memcmp(tag, "name", 4)) name = t;
        else if (!std::memcmp(tag, "OS/2", 4)) os2 = t;
        else if (!std::memcmp(tag, "glyf", 4)) glyf = t;
        else if (!std::memcmp(tag, "loca", 4)) loca = t;
        else if (!std::memcmp(tag, "CFF ", 4) || !std::memcmp(tag, "CFF2", 4)) cff = t;
    }

    if (!head.found || head.length < 54) invalid("falta la tabla head");
    if (!maxp.found || !cmap.found) invalid("faltan tablas obligatorias");
    if (maxp.length < 6) invalid("tabla maxp dañada");
    // stb recorre los registros de cmap sin comprobar su tamaño: se verifica aquí
    if (cmap.length < 4 || 4 + 8 * static_cast<size_t>(r.u16(cmap.offset + 2)) > cmap.length) invalid("tabla cmap dañada");
    if (!name.found) invalid("falta la tabla name");
    if (md.format == "ttf" ? !(glyf.found && loca.found) : !cff.found) invalid("faltan los contornos de los glifos");
    if (r.u32(head.offset + 12) != 0x5F0F3CF5) invalid("tabla head dañada");
    if (r.u16(head.offset + 18) == 0) invalid("tabla head dañada");

    // Con la estructura ya comprobada, stb valida los contornos TrueType. Los CFF (.otf) no pasan por
    // stb: su lector de CFF confía en los índices internos y aquí no aporta nada (solo leemos nombres).
    if (md.format == "ttf") {
        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, r.data, 0)) invalid("no se pudo leer la fuente");
    }

    md.family = name_string(r, name, 16);
    if (md.family.empty()) md.family = name_string(r, name, 1);
    md.subfamily = name_string(r, name, 17);
    if (md.subfamily.empty()) md.subfamily = name_string(r, name, 2);
    if (md.family.empty()) throw FontError(FontError::Kind::Invalid, "La fuente no declara un nombre de familia");

    if (os2.found && os2.length >= 64) {
        const int w = r.u16(os2.offset + 4);
        if (w >= 1 && w <= 1000) md.weight = w;
        md.fs_type = r.u16(os2.offset + 8);
        md.italic = (r.u16(os2.offset + 62) & 0x0001) != 0;  // fsSelection, bit 0
    } else {
        // sin OS/2: macStyle de head (bit 0 = negrita, bit 1 = itálica)
        const int mac = r.u16(head.offset + 44);
        md.italic = (mac & 0x2) != 0;
        md.weight = (mac & 0x1) ? 700 : 400;
    }
    return md;
}

}  // namespace wcatalog
