#pragma once

#include <httplib.h>

#include <filesystem>
#include <functional>

#include "documents/document_context.hpp"
#include "documents/document_renderer.hpp"
#include "documents/document_exporter.hpp"
#include "documents/document_service.hpp"
#include "documents/formats.hpp"

namespace wcatalog {

// GET /api/formatos
void register_format_routes(httplib::Server& server, const FormatCatalog& formats);

// GET/POST /api/documentos, GET/PUT/DELETE /api/documentos/{id},
// POST /api/documentos/{id}/portada (multipart "archivo", o JSON {foto_id}),
// GET /api/casas/{id}/documentos, GET /media/documentos/{id}/portada_opt.jpg
void register_document_routes(httplib::Server& server, DocumentService& documents);

// POST /api/documentos/{id}/exportar  {formatos?: [...]}  -> resultado por formato.
// Otra exportación en curso -> 409 con código `exportacion_en_curso`.
void register_export_routes(httplib::Server& server, DocumentExporter& exporter);

// POST /api/sistema/abrir-ruta  {ruta}: abre una carpeta, un HTML o un PDF dentro de `outputs_dir` con la
// aplicación predeterminada de Windows. Cualquier otra ruta -> 400. `opener` se inyecta para poder probarlo.
using PathOpener = std::function<bool(const std::filesystem::path&)>;
void register_open_path_routes(httplib::Server& server, std::filesystem::path outputs_dir, PathOpener opener);

// GET /render/{documento_id}/{formato_id}: el documento en HTML, en modo vista previa y sin caché.
// Formato desconocido -> 400; documento inexistente -> 404; error de plantilla -> 500 con el archivo y la línea.
void register_render_routes(httplib::Server& server, const DocumentContextBuilder& builder,
                            const DocumentRenderer& renderer, const FormatCatalog& formats);

}  // namespace wcatalog
