#pragma once

#include <cstdint>

namespace wcatalog {

inline constexpr double kMaxPrice = 1e10;

// Importe decimal -> centavos, redondeando a 2 decimales (mitad hacia arriba).
// Lanza std::invalid_argument si es negativo, no finito o supera kMaxPrice.
std::int64_t price_to_cents(double price);

double cents_to_price(std::int64_t cents);

}  // namespace wcatalog
