#pragma once

#include <string>
#include <vector>

#include "fonts/font_library.hpp"
#include "settings/settings_service.hpp"

namespace wcatalog {

// Genera la hoja CSS del tema a partir de los ajustes vigentes: variables de color y de
// fuente, y un @font-face por cada variante de las familias elegidas.
//
// font_base_url: prefijo de las URL de los archivos de fuente (p. ej. "/media/fuentes/" para
// la vista previa o "assets/fuentes/" para la exportación).
// origin_subdirs: true -> {base}incluidas/{archivo} y {base}importadas/{archivo};
//                 false -> {base}{archivo}.
// Si una familia elegida ya no existe, se usa la fuente por defecto.
// Variantes de fuente que usa el tema (las familias elegidas para títulos y texto, sin repetir).
std::vector<FontVariant> theme_font_variants(const SettingsService& settings, const FontLibrary& fonts);

std::string build_theme_css(const SettingsService& settings, const FontLibrary& fonts,
                            const std::string& font_base_url, bool origin_subdirs = true);

}  // namespace wcatalog
