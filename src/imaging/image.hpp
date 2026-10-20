#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wcatalog {

// Imagen en memoria, píxeles entrelazados fila a fila.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;  // 1..4
    std::vector<std::uint8_t> pixels;

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

}  // namespace wcatalog
