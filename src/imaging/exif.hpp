#pragma once

#include <cstddef>
#include <cstdint>

#include "imaging/image.hpp"

namespace wcatalog {

// Orientación EXIF (tag 0x0112) de un JPEG, de 1 a 8. Devuelve 1 (normal) si el archivo no
// es JPEG, no tiene EXIF, el tag falta o los datos están truncados o fuera de rango.
int read_jpeg_orientation(const std::uint8_t* data, std::size_t size);

// Aplica la orientación EXIF 1..8 a los píxeles, de modo que la imagen quede vista "derecha".
// Los valores 5..8 intercambian ancho y alto. Cualquier otro valor equivale a 1.
Image apply_orientation(const Image& src, int orientation);

}  // namespace wcatalog
