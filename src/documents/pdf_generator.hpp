#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace wcatalog {

// Resultado de un intento de PDF. Si falla, `reason` es uno de:
//   edge_no_encontrado | tiempo_agotado | fallo_edge
// y `message` explica el motivo para el usuario.
struct PdfResult {
    bool ok = false;
    std::string reason;
    std::string message;
};

// Genera un PDF a partir de un HTML. `pages` sirve para calcular el tiempo máximo.
class PdfGenerator {
public:
    virtual ~PdfGenerator() = default;
    virtual PdfResult generate(const std::filesystem::path& html, const std::filesystem::path& pdf, int pages) = 0;
};

// URL file:/// de una ruta de Windows, con los caracteres especiales codificados.
std::string path_to_file_url(const std::filesystem::path& path);

// PDF con Edge en modo headless y un perfil propio. El PDF respeta el @page del HTML (tamaño exacto),
// mantiene el texto como texto e incrusta las fuentes.
class EdgePdfGenerator : public PdfGenerator {
public:
    struct Options {
        // Localiza msedge.exe; por defecto, find_edge().
        std::function<std::optional<std::filesystem::path>()> find_edge;
        std::filesystem::path profile_dir;  // por defecto, edge_headless_profile_dir()
        int base_timeout_seconds = 30;      // + per_page_seconds por página
        int per_page_seconds = 2;
        // Solo para pruebas: línea de comandos alternativa (edge, html, pdf) -> texto.
        std::function<std::wstring(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&)>
            command_override;
    };

    EdgePdfGenerator();
    explicit EdgePdfGenerator(Options options);

    PdfResult generate(const std::filesystem::path& html, const std::filesystem::path& pdf, int pages) override;

private:
    Options options_;
};

}  // namespace wcatalog
