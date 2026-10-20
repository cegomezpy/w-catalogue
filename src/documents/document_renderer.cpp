#include "documents/document_renderer.hpp"

namespace wcatalog {

std::string DocumentRenderer::render(const nlohmann::json& context) const {
    nlohmann::json ctx = context;
    // las hojas de estilo van sin escapar (claves *_css); el resto del contexto se escapa al renderizar
    ctx["base_css"] = engine_.read_file("base.css");
    const bool flyer = ctx.at("documento").at("tipo") == "flyer";
    return engine_.render(flyer ? "flyer.html" : "catalogo.html", ctx);
}

}  // namespace wcatalog
