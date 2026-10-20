#include "documents/template_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <regex>
#include <vector>
#include <fstream>
#include <sstream>

#include <inja.hpp>

#include "documents/document_text.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace wcatalog {

namespace {

// En Inja una cadena vacía cuenta como verdadera; aquí un texto vacío equivale a "no hay dato" (null), para que
// `{% if x %}` omita el bloque.
void escape_strings(Json& j) {
    if (j.is_string()) {
        if (j.get_ref<const std::string&>().empty()) j = nullptr;
        else j = html_escape(j.get<std::string>());
    } else if (j.is_array() || j.is_object()) {
        for (auto& item : j) escape_strings(item);
    }
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Todo texto de los datos va a HTML ya escapado, salvo las hojas de estilo (claves *_css de primer nivel).
Json escaped_copy(const Json& data) {
    Json out = data;
    if (!out.is_object()) {
        escape_strings(out);
        return out;
    }
    for (auto it = out.begin(); it != out.end(); ++it) {
        if (!ends_with(it.key(), "_css")) escape_strings(it.value());
    }
    return out;
}

}  // namespace

TemplateEngine::TemplateEngine(fs::path templates_dir) : dir_(std::move(templates_dir)) {}

std::string TemplateEngine::read_file(const std::string& name) const {
    std::ifstream in(dir_ / fs::u8path(name), std::ios::binary);
    if (!in) throw TemplateError("No se encontró el archivo de plantilla: " + name);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

// Localiza el archivo con el error de sintaxis. Inja informa la línea pero, si el error está en un include,
// atribuye el fallo a la plantilla que lo incluye: se analiza cada plantilla por separado y se toma la que
// falla sin que falle ninguna de las que incluye.
std::string TemplateEngine::find_broken_file(inja::Environment& env, const std::string& fallback,
                                              std::size_t& line) const {
    static const std::regex include_re(R"re(\{%-?\s*include\s+"([^"]+)")re");
    struct Broken {
        std::string file;
        std::size_t line;
        std::vector<std::string> includes;
    };
    std::vector<Broken> broken;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".html") continue;
        const fs::path rel_path = fs::relative(entry.path(), dir_, ec);
        const std::string rel = to_utf8(rel_path.generic_u8string());
        try {
            env.parse_template(rel);
        } catch (const inja::InjaError& e) {
            Broken b{rel, e.location.line, {}};
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (std::sregex_iterator it(text.begin(), text.end(), include_re), end; it != end; ++it) {
                const fs::path inc = (rel_path.parent_path() / fs::u8path((*it)[1].str())).lexically_normal();
                b.includes.push_back(to_utf8(inc.generic_u8string()));
            }
            broken.push_back(std::move(b));
        } catch (...) {
        }
    }
    for (const Broken& b : broken) {
        const bool via_include = std::any_of(b.includes.begin(), b.includes.end(), [&](const std::string& inc) {
            return std::any_of(broken.begin(), broken.end(), [&](const Broken& o) { return o.file == inc; });
        });
        if (via_include) continue;
        line = b.line;
        return b.file;
    }
    return fallback;
}

std::string TemplateEngine::render(const std::string& name, const Json& data) const {
    inja::Environment env(to_utf8(dir_) + "/");
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);
    // importe(precio, moneda): el formato de los importes se cambia en un solo sitio
    env.add_callback("importe", 2, [](inja::Arguments& args) {
        const double amount = args.at(0)->get<double>();
        const std::string currency = args.at(1)->is_string() ? args.at(1)->get<std::string>() : "";
        return format_price(static_cast<std::int64_t>(std::llround(amount * 100.0)), currency);
    });
    try {
        return env.render_file(name, escaped_copy(data));
    } catch (const inja::InjaError& e) {
        std::size_t line = e.location.line;
        const std::string file = e.type == "parser_error" || e.type == "lexer_error"
                                     ? find_broken_file(env, name, line)
                                     : name;
        std::string where = "plantilla " + file;
        if (line > 0) where += ", línea " + std::to_string(line);
        throw TemplateError("Error en la " + where + ": " + e.message);
    } catch (const Json::exception& e) {
        throw TemplateError("Error de datos en la plantilla " + name + ": " + e.what());
    }
}

}  // namespace wcatalog
