#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "db/database.hpp"
#include "documents/formats.hpp"
#include "domain/errors.hpp"
#include "http/json_response.hpp"

namespace wcatalog {

constexpr int kMaxDocumentTitleChars = 200;
constexpr int kMaxDocumentDescriptionChars = 5000;
constexpr int kMaxShortDescriptionChars = 300;
constexpr int kMaxHousesPerCatalog = 100;

struct DocumentPaths {
    std::filesystem::path documents_dir;  // data/documentos
    std::filesystem::path tmp_dir;        // data/tmp
};

// Flyers (una casa) y catálogos (varias casas ordenadas): datos propios del documento. El contenido
// de las casas no se copia; se lee de ellas al vista previa y al exportar.
class DocumentService {
public:
    DocumentService(Database& db, const FormatCatalog& formats, DocumentPaths paths);

    // [{id, tipo, titulo, casas, actualizado}] del más recientemente modificado al más antiguo.
    // `titulo` es el efectivo (en un flyer sin título propio, el de la casa).
    Json list() const;

    // Documento completo. NotFoundError si no existe.
    //   {id, tipo, titulo, descripcion, titulo_efectivo, descripcion_efectiva,
    //    casas: [{casa_id, titulo, orden, descripcion_corta, descripcion_corta_efectiva}],
    //    portada: null | {origen: "subida"|"casa", foto_id, url}, formatos_preferidos, creado, actualizado}
    Json get(std::int64_t id) const;

    // Flyer: {tipo: "flyer", casa_id, titulo?, descripcion?}.
    // Catálogo: {tipo: "catalogo", titulo, descripcion?, casas: [{casa_id, descripcion_corta?}], formatos_preferidos?}.
    // ValidationFailed con `fields` si algo no es válido.
    Json create(const Json& payload);

    // Reemplaza los datos editables (título, descripción, formatos y, en catálogos, las casas con su orden).
    Json update(std::int64_t id, const Json& payload);

    // Elimina el documento y su carpeta de portada.
    void remove(std::int64_t id);

    // Documentos que usa una casa y qué les pasa si se elimina:
    // [{id, tipo, titulo, efecto: "se_elimina"|"se_quita"}]
    Json for_house(std::int64_t house_id) const;

    // Portada de un catálogo: sube una imagen (JPEG/PNG) o elige una foto de una casa incluida.
    //   subir: bytes de la imagen. ValidationFailed/PhotoRejected si no es admisible.
    Json set_cover_upload(std::int64_t id, const std::uint8_t* data, std::size_t size);
    //   {foto_id: N} elige una foto de las casas del catálogo; {foto_id: null} la quita.
    Json set_cover_photo(std::int64_t id, const Json& payload);

    // Archivo servible de la portada subida (opt) o vacío si no existe.
    std::filesystem::path cover_file(std::int64_t id) const;

    // Borra los temporales de portadas interrumpidas con más de `max_age_hours`.
    void cleanup_tmp(int max_age_hours = 1);

private:
    struct Parsed;
    Parsed parse(const Json& payload, bool creating, const std::string& type) const;
    void write_houses(std::int64_t doc_id, const Parsed& p);
    void drop_cover_if_invalid(std::int64_t doc_id);
    std::filesystem::path doc_dir(std::int64_t id) const;
    std::string type_of(std::int64_t id) const;
    void remove_cover_files(std::int64_t id) const;

    Database& db_;
    const FormatCatalog& formats_;
    DocumentPaths paths_;
};

}  // namespace wcatalog
