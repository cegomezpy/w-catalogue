#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace wcatalog {

constexpr std::size_t kPhotoMaxBytes = 25u * 1024 * 1024;
constexpr long long kPhotoMaxPixels = 60'000'000;
constexpr int kPhotoOptSide = 2000;
constexpr int kPhotoOptQuality = 85;
constexpr int kPhotoMinSide = 400;
constexpr int kPhotoMinQuality = 80;

// La foto no se puede aceptar (formato, tamaño o archivo dañado). El mensaje es para el usuario.
struct PhotoRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ProcessedPhoto {
    std::string original_ext;  // "jpg" o "png"
    int width = 0;             // de la versión optimizada, ya orientada
    int height = 0;
    std::vector<std::uint8_t> opt_jpeg;  // lado mayor <= 2000 px, calidad 85
    std::vector<std::uint8_t> min_jpeg;  // lado mayor <= 400 px, calidad 80
};

// Valida, decodifica, corrige la orientación EXIF, compone el alfa sobre blanco y genera las
// dos versiones sin ampliar. Lanza PhotoRejected si el archivo no es admisible.
ProcessedPhoto process_photo(const std::uint8_t* data, std::size_t size);

// Nuevo tamaño para que el lado mayor no pase de `max_side`, sin ampliar (devuelve el mismo si ya cabe).
void fit_within(int width, int height, int max_side, int& out_w, int& out_h);

}  // namespace wcatalog
