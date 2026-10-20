#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "db/database.hpp"
#include "domain/errors.hpp"
#include "http/json_response.hpp"
#include "imaging/photo_pipeline.hpp"

namespace wcatalog {

constexpr int kMaxPhotosPerHouse = 30;
constexpr int kMaxCaptionChars = 120;

struct PhotoPaths {
    std::filesystem::path photos_dir;  // data/fotos
    std::filesystem::path tmp_dir;     // data/tmp
};

// Fotos de cada casa: subida atómica, orden/portada, pies y eliminación, con los archivos en
// data/fotos/<casa_id>/<foto_id>_{orig,opt,min}.
class PhotoService {
public:
    PhotoService(Database& db, PhotoPaths paths);

    // Procesa y guarda una foto al final del orden. NotFoundError si la casa no existe,
    // ConflictError si ya tiene 30, PhotoRejected si el archivo no es admisible.
    // Si algo falla no queda ni registro ni archivos.
    Json upload(std::int64_t house_id, const std::uint8_t* data, std::size_t size);

    // [{id, orden, pie, ancho, alto, url_opt, url_min}] en orden; la primera es la portada.
    Json list(std::int64_t house_id) const;

    // `ids` debe ser exactamente el conjunto de fotos de la casa. Devuelve la lista nueva.
    Json reorder(std::int64_t house_id, const Json& ids);

    // Cambia el pie (hasta 120 caracteres; "" lo quita). Devuelve la foto.
    Json set_caption(std::int64_t photo_id, const Json& payload);

    // Borra el registro y luego los archivos de la foto. NotFoundError si no existe.
    void remove(std::int64_t photo_id);

    // Borra data/fotos/<casa_id>/ (tras eliminar la casa). No falla si no existe.
    void remove_house_folder(std::int64_t house_id);

    // casa_id -> URL de la miniatura de su portada, solo para las casas con fotos.
    std::map<std::int64_t, std::string> cover_thumbnails() const;

    // Borra los temporales de subidas interrumpidas con más de `max_age_hours`.
    void cleanup_tmp(int max_age_hours = 1);

    // Registra en el log las carpetas de fotos sin casa. No borra nada. Devuelve sus nombres.
    std::vector<std::string> log_orphan_folders() const;

    // Archivo servible (opt o min) o vacío si el nombre no es válido o no existe.
    std::filesystem::path media_file(const std::string& house_dir, const std::string& file) const;

private:
    bool house_exists(std::int64_t id) const;
    int count_for(std::int64_t house_id) const;
    Json photo_json(std::int64_t house_id, std::int64_t id, std::int64_t order, const std::string& caption, int w,
                    int h) const;
    std::filesystem::path house_dir(std::int64_t house_id) const;
    void remove_photo_files(std::int64_t house_id, std::int64_t photo_id) const;

    Database& db_;
    PhotoPaths paths_;
};

}  // namespace wcatalog
