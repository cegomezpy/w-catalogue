#include "domain/text_fold.hpp"

#include <cstdint>

namespace wcatalog {

namespace {

// Equivalente ASCII en minúsculas de U+00C0..U+017F (Latin-1 Supplement y Latin Extended-A).
// Tabla generada a partir de la descomposición Unicode NFKD, con las letras que no se
// descomponen (æ, œ, ð, đ, ø, þ, ß...) escritas a mano.
const char* const kLatinFold[0x180 - 0xC0] = {
    "a", "a", "a", "a", "a", "a", "ae", "c",  // U+00C0..
    "e", "e", "e", "e", "i", "i", "i", "i",  // U+00C8..
    "d", "n", "o", "o", "o", "o", "o", " ",  // U+00D0..
    "o", "u", "u", "u", "u", "y", "th", "ss",  // U+00D8..
    "a", "a", "a", "a", "a", "a", "ae", "c",  // U+00E0..
    "e", "e", "e", "e", "i", "i", "i", "i",  // U+00E8..
    "d", "n", "o", "o", "o", "o", "o", " ",  // U+00F0..
    "o", "u", "u", "u", "u", "y", "th", "y",  // U+00F8..
    "a", "a", "a", "a", "a", "a", "c", "c",  // U+0100..
    "c", "c", "c", "c", "c", "c", "d", "d",  // U+0108..
    "d", "d", "e", "e", "e", "e", "e", "e",  // U+0110..
    "e", "e", "e", "e", "g", "g", "g", "g",  // U+0118..
    "g", "g", "g", "g", "h", "h", "h", "h",  // U+0120..
    "i", "i", "i", "i", "i", "i", "i", "i",  // U+0128..
    "i", "i", "ij", "ij", "j", "j", "k", "k",  // U+0130..
    "k", "l", "l", "l", "l", "l", "l", " ",  // U+0138..
    " ", "l", "l", "n", "n", "n", "n", "n",  // U+0140..
    "n", "n", "n", "n", "o", "o", "o", "o",  // U+0148..
    "o", "o", "oe", "oe", "r", "r", "r", "r",  // U+0150..
    "r", "r", "s", "s", "s", "s", "s", "s",  // U+0158..
    "s", "s", "t", "t", "t", "t", "t", "t",  // U+0160..
    "u", "u", "u", "u", "u", "u", "u", "u",  // U+0168..
    "u", "u", "u", "u", "w", "w", "y", "y",  // U+0170..
    "y", "z", "z", "z", "z", "z", "z", "s",  // U+0178..
};

// Decodifica un punto de código UTF-8 y avanza `i`. Los bytes inválidos se devuelven tal cual.
std::uint32_t next_codepoint(const std::string& s, size_t& i, bool& invalid) {
    invalid = false;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    if (c < 0x80) { ++i; return c; }
    if ((c & 0xE0) == 0xC0 && cont(1)) {
        std::uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        std::uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                           (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        std::uint32_t cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                           ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                           (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    invalid = true;
    return c;
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

}  // namespace

std::string fold_text(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        bool invalid = false;
        const std::uint32_t cp = next_codepoint(utf8, i, invalid);
        if (invalid) {
            out += static_cast<char>(cp);  // byte suelto no válido: se conserva
            continue;
        }
        if (cp >= 'A' && cp <= 'Z') {
            out += static_cast<char>(cp - 'A' + 'a');
        } else if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp >= 0xC0 && cp < 0x180) {
            out += kLatinFold[cp - 0xC0];
        } else {
            append_utf8(out, cp);  // fuera del rango latino se conserva
        }
    }
    return out;
}

}  // namespace wcatalog
