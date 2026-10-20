#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wcatalog {

constexpr std::size_t kShortDescriptionChars = 180;

// Descripción corta automática: si el texto cabe en `max_chars` caracteres se devuelve tal cual;
// si no, se recorta sin partir palabras y se termina en "…".
std::string make_short_description(const std::string& text, std::size_t max_chars = kShortDescriptionChars);

// Importe con separador de miles "," y decimales solo si los hay: 8500000 centavos -> "85,000".
std::string format_amount(std::int64_t cents);

// Importe con su moneda: "85,000 USD" (sin moneda, solo el importe).
std::string format_price(std::int64_t cents, const std::string& currency);

// Número para mostrar: sin decimales si es entero, con hasta 2 si no ("120", "85.5").
std::string format_number(double value);

// Texto de HTML: escapa & < > " '.
std::string html_escape(const std::string& text);

// Título para el nombre de una carpeta: minúsculas, sin tildes, con guiones, de hasta `max_chars`.
// Si no queda nada utilizable devuelve "documento".
std::string slugify_title(const std::string& title, std::size_t max_chars = 40);

}  // namespace wcatalog
