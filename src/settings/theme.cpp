#include "settings/theme.hpp"

#include <set>
#include <sstream>

namespace wcatalog {

namespace {

std::string css_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out;
}

// Familia vigente para un rol: la elegida si existe; si no, la de por defecto.
std::string resolve_family(const SettingsService& settings, const FontLibrary& fonts, const char* key) {
    std::string family = settings.value(key);
    if (!fonts.family_exists(family)) family = SettingsService::default_value(key);
    return family;
}

std::string generic_for(const FontLibrary& fonts, const std::string& family) {
    auto vs = fonts.family_variants(family);
    return vs.empty() ? "sans-serif" : vs.front().generic;
}

}  // namespace

std::vector<FontVariant> theme_font_variants(const SettingsService& settings, const FontLibrary& fonts) {
    const std::string titles = resolve_family(settings, fonts, SettingsService::kFuenteTitulos);
    const std::string text = resolve_family(settings, fonts, SettingsService::kFuenteTexto);
    std::vector<FontVariant> out;
    std::set<std::string> done;
    for (const std::string& family : {titles, text}) {
        if (!done.insert(family).second) continue;
        for (const FontVariant& v : fonts.family_variants(family)) out.push_back(v);
    }
    return out;
}

std::string build_theme_css(const SettingsService& settings, const FontLibrary& fonts,
                            const std::string& font_base_url, bool origin_subdirs) {
    const std::string titles = resolve_family(settings, fonts, SettingsService::kFuenteTitulos);
    const std::string text = resolve_family(settings, fonts, SettingsService::kFuenteTexto);

    std::ostringstream css;
    css << "/* Tema de WCatalog (generado) */\n";

    std::set<std::string> done;
    for (const std::string& family : {titles, text}) {
        if (!done.insert(family).second) continue;
        for (const FontVariant& v : fonts.family_variants(family)) {
            std::string url = font_base_url;
            if (origin_subdirs) url += v.origin == "incluida" ? "incluidas/" : "importadas/";
            url += v.file;
            css << "@font-face {\n"
                << "  font-family: \"" << css_escape(family) << "\";\n"
                << "  src: url(\"" << css_escape(url) << "\") format(\"" << (v.format == "otf" ? "opentype" : "truetype")
                << "\");\n"
                << "  font-weight: " << v.weight << ";\n"
                << "  font-style: " << v.style << ";\n"
                << "  font-display: block;\n"
                << "}\n";
        }
    }

    css << ":root {\n"
        << "  --color-primario: " << settings.value("tema.colorPrimario") << ";\n"
        << "  --color-secundario: " << settings.value("tema.colorSecundario") << ";\n"
        << "  --color-terciario: " << settings.value("tema.colorTerciario") << ";\n"
        << "  --fuente-titulos: \"" << css_escape(titles) << "\", " << generic_for(fonts, titles) << ";\n"
        << "  --fuente-texto: \"" << css_escape(text) << "\", " << generic_for(fonts, text) << ";\n"
        << "}\n";
    return css.str();
}

}  // namespace wcatalog
