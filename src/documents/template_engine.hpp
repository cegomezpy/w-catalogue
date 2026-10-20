#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace inja {
class Environment;
}

namespace wcatalog {

// Error de una plantilla (sintaxis o datos); el mensaje incluye el archivo y, si se conoce, la línea.
struct TemplateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Renderiza las plantillas Inja de templates/ a HTML estático.
class TemplateEngine {
public:
    explicit TemplateEngine(std::filesystem::path templates_dir);

    // Renderiza templates/<name> (p. ej. "flyer.html") con los datos. Lanza TemplateError.
    std::string render(const std::string& name, const nlohmann::json& data) const;

    // Texto de un archivo de templates/ (p. ej. "base.css"). Lanza TemplateError si no existe.
    std::string read_file(const std::string& name) const;

    const std::filesystem::path& dir() const { return dir_; }

private:
    std::string find_broken_file(inja::Environment& env, const std::string& fallback, std::size_t& line) const;

    std::filesystem::path dir_;
};

}  // namespace wcatalog
