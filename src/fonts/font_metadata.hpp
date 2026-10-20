#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace wcatalog {

struct FontError : std::runtime_error {
    enum class Kind { Invalid, Conflict, NotFound };
    FontError(Kind k, const std::string& msg) : std::runtime_error(msg), kind(k) {}
    Kind kind;
};

struct FontMetadata {
    std::string family;     // nameID 16, con respaldo al 1
    std::string subfamily;  // nameID 17, con respaldo al 2
    int weight = 400;       // usWeightClass (OS/2)
    bool italic = false;    // bit de itálica de fsSelection
    int fs_type = 0;        // OS/2 fsType; 0x0002 = incrustación restringida
    std::string format;     // "ttf" | "otf"

    bool embedding_restricted() const { return (fs_type & 0x0002) != 0; }
};

inline constexpr std::size_t kMaxFontBytes = 10 * 1024 * 1024;

// Valida firma, tamaño y que stb_truetype pueda abrirla; lee familia, peso y estilo.
// Lanza FontError(Invalid) con un mensaje claro si no es una fuente TrueType/OpenType válida.
FontMetadata read_font_metadata(const std::string& bytes);

}  // namespace wcatalog
