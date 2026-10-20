#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "documents/document_service.hpp"
#include "documents/formats.hpp"
#include "fonts/font_library.hpp"
#include "houses/house_service.hpp"
#include "settings/settings_service.hpp"

namespace wcatalog {

enum class RenderMode {
    Preview,  // vista previa en la app: recursos bajo /media/
    Export,   // HTML exportado: recursos relativos bajo assets/
};

// Un archivo que el HTML exportado necesita junto a él: `from` en disco, `to` relativo a assets/.
struct AssetFile {
    std::filesystem::path from;
    std::string to;
};

// Fotos de cada casa que se incluyen en un documento (el resto no se copia ni se muestra).
constexpr std::size_t kFlyerPhotosPerHouse = 5;
constexpr std::size_t kCatalogPhotosPerHouse = 3;

// Instantánea de un documento: todos los datos que muestran las plantillas, ya resueltos. Es válida para
// cualquier formato (`for_format` la adapta) y no cambia aunque después cambien las casas o el tema.
struct DocumentSnapshot {
    Json context;
    std::vector<AssetFile> assets;  // solo en modo Export
};

// Construye el contexto de las plantillas. Toda la lógica de "qué mostrar" (textos por defecto, recorte
// de descripciones, resolución del precio) vive aquí; las plantillas solo deciden dónde y cómo.
class DocumentContextBuilder {
public:
    DocumentContextBuilder(DocumentService& documents, HouseService& houses, const SettingsService& settings,
                           const FontLibrary& fonts, std::filesystem::path photos_dir);

    // NotFoundError si el documento no existe; ValidationFailed si un catálogo no tiene casas
    // (en vista previa se permite, para poder editarlo).
    DocumentSnapshot snapshot(std::int64_t document_id, RenderMode mode) const;

    // Copia de la instantánea para un formato: `formato` y los badges recortados a `max_badges`.
    static Json for_format(const Json& snapshot_context, const DocFormat& format);

private:
    DocumentService& documents_;
    HouseService& houses_;
    const SettingsService& settings_;
    const FontLibrary& fonts_;
    std::filesystem::path photos_dir_;
};

}  // namespace wcatalog
