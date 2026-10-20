#pragma once

// Entorno de pruebas de documentos: base de datos temporal con casas, fotos, fuentes, formatos y plantillas.

#include <filesystem>
#include <map>
#include <memory>

#include "db/migrations.hpp"
#include "documents/document_context.hpp"
#include "documents/document_service.hpp"
#include "documents/formats.hpp"
#include "documents/template_engine.hpp"
#include "fonts/font_library.hpp"
#include "houses/feature_service.hpp"
#include "houses/house_service.hpp"
#include "photo_fixtures.hpp"
#include "photos/photo_service.hpp"
#include "settings/settings_service.hpp"

namespace wcdoc {

namespace fs = std::filesystem;
using namespace wcatalog;
using namespace photo_test;

struct DocEnv {
    fs::path dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<SettingsService> settings;
    std::unique_ptr<FeatureService> features;
    std::unique_ptr<HouseService> houses;
    std::unique_ptr<PhotoService> photos;
    FormatCatalog formats;
    std::unique_ptr<DocumentService> documents;
    std::unique_ptr<FontLibrary> fonts;
    std::unique_ptr<DocumentContextBuilder> builder;
    TemplateEngine engine{fs::path(WCATALOG_SOURCE_DIR) / "templates"};

    explicit DocEnv(const char* name)
        : formats(FormatCatalog::load(fs::path(WCATALOG_SOURCE_DIR) / "templates" / "formatos.json")) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir / "fotos");
        fs::create_directories(dir / "tmp");
        fs::create_directories(dir / "documentos");
        db = open_app_database(dir / "c.db", dir / "backups");
        settings = std::make_unique<SettingsService>(*db);
        features = std::make_unique<FeatureService>(*db);
        houses = std::make_unique<HouseService>(*db, *settings, *features);
        photos = std::make_unique<PhotoService>(*db, PhotoPaths{dir / "fotos", dir / "tmp"});
        houses->set_photo_service(photos.get());
        documents = std::make_unique<DocumentService>(*db, formats, DocumentPaths{dir / "documentos", dir / "tmp"});
        fonts = std::make_unique<FontLibrary>(
            *db, *settings, FontPaths{fs::path(WCATALOG_SOURCE_DIR) / "fonts", dir / "fuentes", dir / "tmp"});
        fonts->load_manifest();
        settings->set_font_validator([this](const std::string& f) { return fonts->family_exists(f); });
        builder = std::make_unique<DocumentContextBuilder>(*documents, *houses, *settings, *fonts, dir / "fotos");
    }
    ~DocEnv() {
        builder.reset();
        fonts.reset();
        documents.reset();
        photos.reset();
        houses.reset();
        features.reset();
        settings.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::int64_t house(const char* title, const std::string& description = "") {
        return houses->create(Json{{"titulo", title}, {"descripcion", description}})["id"].get<std::int64_t>();
    }
    std::int64_t photo(std::int64_t house_id) {
        const Bytes jpeg = plain_jpeg();
        return photos->upload(house_id, jpeg.data(), jpeg.size())["id"].get<std::int64_t>();
    }
    static Json casas(std::initializer_list<std::int64_t> house_ids) {
        Json list = Json::array();
        for (std::int64_t id : house_ids) list.push_back(Json{{"casa_id", id}});
        return list;
    }
    Json catalog(const char* title, std::initializer_list<std::int64_t> house_ids) {
        return documents->create(Json{{"tipo", "catalogo"}, {"titulo", title}, {"casas", casas(house_ids)}});
    }
    std::map<std::string, std::string> errors_of(const Json& payload) {
        try {
            documents->create(payload);
        } catch (const ValidationFailed& v) {
            return v.fields;
        }
        return {};
    }
    static std::vector<std::int64_t> house_order(const Json& doc) {
        std::vector<std::int64_t> ids;
        for (const Json& c : doc["casas"]) ids.push_back(c["casa_id"].get<std::int64_t>());
        return ids;
    }
};

}  // namespace wcdoc
