#include "domain/money.hpp"

#include <cmath>
#include <stdexcept>

namespace wcatalog {

std::int64_t price_to_cents(double price) {
    if (!std::isfinite(price)) throw std::invalid_argument("El precio no es un número válido");
    if (price < 0) throw std::invalid_argument("El precio no puede ser negativo");
    if (price > kMaxPrice) throw std::invalid_argument("El precio es demasiado grande");
    // el épsilon compensa representaciones como 1.005 = 1.00499999999999989...
    return static_cast<std::int64_t>(std::llround(price * 100.0 + 1e-7));
}

double cents_to_price(std::int64_t cents) { return static_cast<double>(cents) / 100.0; }

}  // namespace wcatalog
