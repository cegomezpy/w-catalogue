#include "documents/document_exporter.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "documents/export_files.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

int count_pages(const std::string& html) {
    int pages = 0;
    const std::string needle = "<section class=\"pagina";
    for (std::size_t pos = html.find(needle); pos != std::string::npos; pos = html.find(needle, pos + needle.size())) {
        ++pages;
    }
    return pages > 0 ? pages : 1;
}

// Borra la carpeta de una exportación fallida al salir de ámbito, salvo que se confirme.
struct FolderGuard {
    fs::path folder;
    bool keep = false;
    ~FolderGuard() {
        if (keep || folder.empty()) return;
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
};

}  // namespace

DocumentExporter::DocumentExporter(DocumentContextBuilder& builder, const DocumentRenderer& renderer,
                                   DocumentService& documents, const FormatCatalog& formats, fs::path outputs_dir,
                                   PdfGenerator& pdf)
    : builder_(builder),
      renderer_(renderer),
      documents_(documents),
      formats_(formats),
      outputs_dir_(std::move(outputs_dir)),
      pdf_(pdf) {}

std::vector<DocFormat> DocumentExporter::choose_formats(const Json& payload, const Json& document) const {
    std::vector<std::string> wanted;
    if (payload.is_object() && payload.contains("formatos")) {
        const Json& list = payload["formatos"];
        if (!list.is_array() || list.empty()) throw ValidationFailed({{"formatos", "Elija al menos un formato"}});
        for (const Json& id : list) {
            if (!id.is_string() || !formats_.find(id.get<std::string>())) {
                throw ValidationFailed({{"formatos", "Formato desconocido"}});
            }
            wanted.push_back(id.get<std::string>());
        }
    } else if (document["formatos_preferidos"].is_array() && !document["formatos_preferidos"].empty()) {
        for (const Json& id : document["formatos_preferidos"]) wanted.push_back(id.get<std::string>());
    }

    // en el orden del catálogo de formatos y sin repetir
    std::vector<DocFormat> out;
    for (const DocFormat& f : formats_.all()) {
        if (wanted.empty() || std::find(wanted.begin(), wanted.end(), f.id) != wanted.end()) out.push_back(f);
    }
    return out;
}

Json DocumentExporter::export_document(std::int64_t document_id, const Json& payload) {
    std::unique_lock<std::mutex> lock(export_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw ExportBusyError();

    const Json document = documents_.get(document_id);
    const std::vector<DocFormat> chosen = choose_formats(payload, document);

    // La instantánea se toma una sola vez: lo exportado no cambia aunque se edite después
    const DocumentSnapshot snapshot = builder_.snapshot(document_id, RenderMode::Export);

    const std::string type = document["tipo"].get<std::string>();
    const std::string name = output_folder_name(current_stamp(), snapshot.context["documento"]["titulo"].get<std::string>(), type);
    FolderGuard guard{create_output_folder(outputs_dir_, name)};
    const fs::path folder = guard.folder;
    log().info("Exportación del documento " + std::to_string(document_id) + " a " + to_utf8(folder));

    // Hasta que todos los HTML están escritos, cualquier error deja la carpeta sin rastro
    std::vector<std::pair<DocFormat, std::string>> pages;  // formato, HTML
    copy_assets(folder, snapshot.assets);
    for (const DocFormat& f : chosen) {
        std::string html = renderer_.render(DocumentContextBuilder::for_format(snapshot.context, f));
        write_text_file(folder / fs::u8path(f.id + ".html"), html);
        pages.emplace_back(f, std::move(html));
    }
    guard.keep = true;  // desde aquí el HTML está garantizado

    Json results = Json::array();
    int pdf_ok = 0;
    for (const auto& [format, html] : pages) {
        const fs::path html_file = folder / fs::u8path(format.id + ".html");
        const fs::path pdf_file = folder / fs::u8path(format.id + ".pdf");
        PdfResult pdf;
        try {
            pdf = pdf_.generate(html_file, pdf_file, count_pages(html));
        } catch (const std::exception& e) {
            pdf.ok = false;
            pdf.reason = "fallo_edge";
            pdf.message = e.what();
        }
        if (pdf.ok) ++pdf_ok;
        else log().warn("PDF no generado (" + format.id + "): " + pdf.reason + " - " + pdf.message);
        results.push_back(Json{{"id", format.id},
                               {"nombre", format.name},
                               {"html", to_utf8(html_file)},
                               {"pdf", pdf.ok ? Json(to_utf8(pdf_file)) : Json(nullptr)},
                               {"pdf_estado", pdf.ok ? "generado" : "fallido"},
                               {"pdf_motivo", pdf.ok ? Json(nullptr) : Json(pdf.reason)},
                               {"pdf_mensaje", pdf.ok ? Json(nullptr) : Json(pdf.message)}});
    }

    Json out = Json{{"carpeta", to_utf8(folder)}, {"formatos", results}};
    try {
        write_text_file(folder / "resultado.json",
                        Json{{"documento_id", document_id},
                             {"titulo", snapshot.context["documento"]["titulo"]},
                             {"tipo", type},
                             {"generado_en", snapshot.context["generado_en"]},
                             {"formatos", results}}
                            .dump(2));
    } catch (const std::exception& e) {
        log().warn(std::string("No se pudo escribir resultado.json: ") + e.what());
    }
    log().info("Exportación terminada: " + std::to_string(pages.size()) + " HTML, " + std::to_string(pdf_ok) + " PDF");
    return out;
}

}  // namespace wcatalog
