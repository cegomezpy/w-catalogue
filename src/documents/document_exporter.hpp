#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>

#include "documents/document_context.hpp"
#include "documents/document_renderer.hpp"
#include "documents/document_service.hpp"
#include "documents/formats.hpp"
#include "documents/pdf_generator.hpp"

namespace wcatalog {

// Ya hay una exportación en curso (HTTP 409, código `exportacion_en_curso`).
struct ExportBusyError : ConflictError {
    ExportBusyError() : ConflictError("Ya hay una exportación en curso; espere a que termine") {}
};

// Exporta un documento: HTML autocontenido por formato (siempre) y PDF por formato (intento).
class DocumentExporter {
public:
    DocumentExporter(DocumentContextBuilder& builder, const DocumentRenderer& renderer, DocumentService& documents,
                     const FormatCatalog& formats, std::filesystem::path outputs_dir, PdfGenerator& pdf);

    // payload: {formatos: ["instagram-4x5", ...]}. Sin `formatos` se usan los preferidos del documento o,
    // si no tiene, todos. Devuelve
    //   {carpeta, formatos: [{id, nombre, html, pdf, pdf_estado: "generado"|"fallido", pdf_motivo, pdf_mensaje}]}
    // con rutas absolutas. Lanza ExportBusyError si hay otra exportación, ValidationFailed (formatos o un
    // catálogo sin casas) y NotFoundError. Un error antes de que los HTML estén escritos borra la carpeta;
    // un fallo de PDF nunca falla la exportación.
    Json export_document(std::int64_t document_id, const Json& payload);

private:
    std::vector<DocFormat> choose_formats(const Json& payload, const Json& document) const;

    DocumentContextBuilder& builder_;
    const DocumentRenderer& renderer_;
    DocumentService& documents_;
    const FormatCatalog& formats_;
    std::filesystem::path outputs_dir_;
    PdfGenerator& pdf_;
    std::mutex export_mutex_;
};

}  // namespace wcatalog
