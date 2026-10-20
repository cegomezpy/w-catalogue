#include "documents/document_text.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

#include "domain/text_fold.hpp"

namespace wcatalog {

namespace {

// Posición (en bytes) tras los primeros `n` caracteres Unicode de un texto UTF-8.
std::size_t byte_offset_of_char(const std::string& s, std::size_t n) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == n) return i;
            ++chars;
        }
    }
    return s.size();
}

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

std::string make_short_description(const std::string& text, std::size_t max_chars) {
    // espacios normalizados: los saltos de línea no tienen sentido en un texto corto
    std::string flat;
    for (char c : text) {
        if (is_space(c)) {
            if (!flat.empty() && flat.back() != ' ') flat += ' ';
        } else {
            flat += c;
        }
    }
    while (!flat.empty() && flat.back() == ' ') flat.pop_back();

    const std::size_t limit = byte_offset_of_char(flat, max_chars);
    if (limit >= flat.size()) return flat;

    std::string cut = flat.substr(0, limit);
    // si el corte cae dentro de una palabra, se vuelve al final de la palabra anterior
    if (!is_space(flat[limit])) {
        const std::size_t sp = cut.find_last_of(' ');
        if (sp != std::string::npos) cut.resize(sp);
    }
    while (!cut.empty() && (is_space(cut.back()) || cut.back() == ',' || cut.back() == ';' || cut.back() == ':' ||
                            cut.back() == '-' || cut.back() == '.')) {
        cut.pop_back();
    }
    return cut + "\xE2\x80\xA6";
}

std::string format_amount(std::int64_t cents) {
    const bool negative = cents < 0;
    const std::int64_t abs_cents = negative ? -cents : cents;
    const std::string whole = std::to_string(abs_cents / 100);
    std::string grouped;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped += ',';
        grouped += whole[i];
    }
    const int frac = static_cast<int>(abs_cents % 100);
    if (frac != 0) {
        char buf[8];
        std::snprintf(buf, sizeof buf, ".%02d", frac);
        grouped += buf;
    }
    return (negative ? "-" : "") + grouped;
}

std::string format_price(std::int64_t cents, const std::string& currency) {
    const std::string amount = format_amount(cents);
    return currency.empty() ? amount : amount + " " + currency;
}

std::string format_number(double value) {
    const double rounded = std::round(value * 100.0) / 100.0;
    char buf[40];
    if (rounded == std::floor(rounded)) {
        std::snprintf(buf, sizeof buf, "%.0f", rounded);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f", rounded);
        std::string s = buf;
        while (!s.empty() && s.back() == '0') s.pop_back();
        return s;
    }
    return buf;
}

std::string html_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string slugify_title(const std::string& title, std::size_t max_chars) {
    const std::string folded = fold_text(title);
    std::string slug;
    bool pending_dash = false;
    for (unsigned char c : folded) {
        if (std::isalnum(c) && c < 0x80) {
            if (pending_dash && !slug.empty()) slug += '-';
            pending_dash = false;
            slug += static_cast<char>(c);
        } else {
            pending_dash = true;
        }
    }
    if (slug.size() > max_chars) {
        slug.resize(max_chars);
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
    }
    return slug.empty() ? "documento" : slug;
}

}  // namespace wcatalog
