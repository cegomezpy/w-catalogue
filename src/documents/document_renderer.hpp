#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "documents/document_context.hpp"
#include "documents/template_engine.hpp"

namespace wcatalog {

// Convierte el contexto de un documento en un formato (ver DocumentContextBuilder::for_format) en el HTML
// completo, con el CSS de templates/base.css y el del tema dentro del propio HTML. La plantilla se elige por
// el tipo de documento (flyer.html o catalogo.html). Lanza TemplateError si una plantilla falla.
class DocumentRenderer {
public:
    explicit DocumentRenderer(const TemplateEngine& engine) : engine_(engine) {}

    std::string render(const nlohmann::json& context) const;

private:
    const TemplateEngine& engine_;
};

}  // namespace wcatalog
