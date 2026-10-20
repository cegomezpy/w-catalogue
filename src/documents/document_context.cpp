#include "documents/document_context.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "documents/document_text.hpp"
#include "settings/theme.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

std::string now_text() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return os.str();
}

// Coordenada con hasta `decimals` decimales, sin ceros sobrantes.
std::string coord_text(double v, int decimals) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

std::string capitalized(const std::string& s) {
    if (s.empty()) return s;
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

void add_datum(Json& list, const char* label, const std::string& value) {
    list.push_back(Json{{"etiqueta", label}, {"valor", value}});
}

Json optional_count(const Json& v) { return v.is_number() ? Json(std::to_string(v.get<std::int64_t>())) : Json(nullptr); }

Json location_context(const Json& house) {
    std::vector<std::string> parts;
    for (const char* key : {"direccion", "barrio", "municipio", "provincia"}) {
        const std::string v = house[key].get<std::string>();
        if (!v.empty()) parts.push_back(v);
    }
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + parts[i];

    Json loc = {{"texto", text.empty() ? Json(nullptr) : Json(text)},
                {"coordenadas_texto", nullptr},
                {"mapa_url", nullptr}};
    if (house["lat"].is_number() && house["lon"].is_number()) {
        const std::string lat = coord_text(house["lat"].get<double>(), 6);
        const std::string lon = coord_text(house["lon"].get<double>(), 6);
        loc["coordenadas_texto"] = coord_text(house["lat"].get<double>(), 5) + ", " + coord_text(house["lon"].get<double>(), 5);
        loc["mapa_url"] = "https://www.google.com/maps?q=" + lat + "," + lon;
    }
    return loc;
}

std::string origin_dir(const std::string& origin) { return origin == "incluida" ? "incluidas" : "importadas"; }

}  // namespace

DocumentContextBuilder::DocumentContextBuilder(DocumentService& documents, HouseService& houses,
                                               const SettingsService& settings, const FontLibrary& fonts,
                                               fs::path photos_dir)
    : documents_(documents), houses_(houses), settings_(settings), fonts_(fonts), photos_dir_(std::move(photos_dir)) {}

DocumentSnapshot DocumentContextBuilder::snapshot(std::int64_t document_id, RenderMode mode) const {
    const Json doc = documents_.get(document_id);
    const bool is_flyer = doc["tipo"] == "flyer";
    const bool exporting = mode == RenderMode::Export;
    if (exporting && doc["casas"].empty()) {
        throw ValidationFailed({{"casas", "El catálogo no tiene casas; agregue al menos una para exportarlo"}});
    }

    DocumentSnapshot snap;
    std::vector<Json> full_houses;  // para ubicar la foto de portada
    Json houses = Json::array();
    const std::size_t photo_limit = is_flyer ? kFlyerPhotosPerHouse : kCatalogPhotosPerHouse;
    const std::string currency_default = settings_.value("general.moneda");

    for (const Json& ref : doc["casas"]) {
        const Json house = houses_.get(ref["casa_id"].get<std::int64_t>());
        full_houses.push_back(house);

        Json c;
        c["id"] = house["id"];
        // el flyer puede tener título y descripción propios; en un catálogo valen los de la casa
        c["titulo"] = is_flyer ? doc["titulo_efectivo"] : house["titulo"];
        c["descripcion"] = is_flyer ? doc["descripcion_efectiva"] : house["descripcion"];
        c["descripcion_corta"] = is_flyer ? Json(make_short_description(doc["descripcion_efectiva"].get<std::string>()))
                                          : ref["descripcion_corta_efectiva"];

        c["operacion_texto"] = house["operacion"] == "venta"      ? Json("En venta")
                               : house["operacion"] == "alquiler" ? Json("En alquiler")
                                                                  : Json(nullptr);
        c["estado_texto"] = house["estado"] == "disponible" ? Json(nullptr) : Json(capitalized(house["estado"].get<std::string>()));

        // precio: importe con moneda, "Consultar" o nada según la opción de la casa
        if (house["precio"].is_number()) {
            const std::string cur = house["moneda"].is_string() ? house["moneda"].get<std::string>() : currency_default;
            c["precio_texto"] = format_price(std::llround(house["precio"].get<double>() * 100.0), cur);
        } else if (house["sin_precio"] == "consultar") {
            c["precio_texto"] = "Consultar";
        } else {
            c["precio_texto"] = nullptr;
        }

        Json data = Json::array();
        if (house["cuartos"].is_number()) add_datum(data, "Cuartos", std::to_string(house["cuartos"].get<std::int64_t>()));
        if (house["banos"].is_number()) add_datum(data, "Baños", std::to_string(house["banos"].get<std::int64_t>()));
        if (house["comedores_independientes"].is_number()) {
            add_datum(data, "Comedores", std::to_string(house["comedores_independientes"].get<std::int64_t>()));
        }
        if (house["niveles"].is_number()) add_datum(data, "Niveles", std::to_string(house["niveles"].get<std::int64_t>()));
        if (house["area_construida_m2"].is_number()) {
            add_datum(data, "Área construida", format_number(house["area_construida_m2"].get<double>()) + " m²");
        }
        if (house["area_terreno_m2"].is_number()) {
            add_datum(data, "Terreno", format_number(house["area_terreno_m2"].get<double>()) + " m²");
        }
        c["datos"] = data;
        c["cuartos"] = optional_count(house["cuartos"]);
        c["banos"] = optional_count(house["banos"]);
        c["ubicacion"] = location_context(house);

        Json badges = Json::array();
        for (const Json& b : house["badges"]) {
            badges.push_back(Json{{"texto", b["texto"]}, {"rol", b["rol"]}, {"icono", b["icono"]}});
        }
        c["badges"] = badges;
        c["badges_restantes"] = 0;

        Json photos = Json::array();
        for (const Json& p : house["fotos"]) {
            if (photos.size() >= photo_limit) break;
            const std::int64_t pid = p["id"].get<std::int64_t>();
            std::string src = p["url_opt"].get<std::string>();
            if (exporting) {
                src = "assets/fotos/" + std::to_string(pid) + ".jpg";
                snap.assets.push_back({photos_dir_ / std::to_string(house["id"].get<std::int64_t>()) /
                                           (std::to_string(pid) + "_opt.jpg"),
                                       "fotos/" + std::to_string(pid) + ".jpg"});
            }
            photos.push_back(Json{{"src", src}, {"ancho", p["ancho"]}, {"alto", p["alto"]}, {"pie", p["pie"].get<std::string>().empty() ? Json(nullptr) : p["pie"]}});
        }
        c["fotos"] = photos;
        c["foto_principal"] = photos.empty() ? Json(nullptr) : photos[0];
        houses.push_back(std::move(c));
    }

    // portada del catálogo
    Json cover = nullptr;
    if (!doc["portada"].is_null()) {
        const Json& pc = doc["portada"];
        cover = Json{{"origen", pc["origen"]}, {"src", pc["url"]}};
        if (exporting) {
            fs::path from;
            if (pc["origen"] == "subida") {
                from = documents_.cover_file(document_id);
            } else {
                for (const Json& h : full_houses) {
                    for (const Json& p : h["fotos"]) {
                        if (p["id"] == pc["foto_id"]) {
                            from = photos_dir_ / std::to_string(h["id"].get<std::int64_t>()) /
                                   (std::to_string(p["id"].get<std::int64_t>()) + "_opt.jpg");
                        }
                    }
                }
            }
            if (from.empty()) {
                cover = nullptr;
            } else {
                cover["src"] = "assets/fotos/portada.jpg";
                snap.assets.push_back({from, "fotos/portada.jpg"});
            }
        }
    }

    // contacto: solo si hay algún dato
    Json contact = {{"nombre", settings_.value("contacto.nombre")},
                    {"telefono", settings_.value("contacto.telefono")},
                    {"whatsapp", settings_.value("contacto.whatsapp")},
                    {"email", settings_.value("contacto.email")},
                    {"web", settings_.value("contacto.web")},
                    {"instagram", settings_.value("contacto.instagram")},
                    {"facebook", settings_.value("contacto.facebook")}};
    const bool has_contact = std::any_of(contact.begin(), contact.end(), [](const Json& v) { return !v.get<std::string>().empty(); });

    // tema: en la exportación las fuentes van junto al HTML
    std::string theme_css;
    if (exporting) {
        theme_css = build_theme_css(settings_, fonts_, "assets/fuentes/", false);
        for (const FontVariant& v : theme_font_variants(settings_, fonts_)) {
            const fs::path from = fonts_.file_path(origin_dir(v.origin), v.file);
            if (!from.empty()) snap.assets.push_back({from, "fuentes/" + v.file});
        }
    } else {
        theme_css = build_theme_css(settings_, fonts_, "/media/fuentes/", true);
    }

    snap.context = Json{{"documento", Json{{"id", doc["id"]},
                                            {"tipo", doc["tipo"]},
                                            {"es_flyer", is_flyer},
                                            {"es_catalogo", !is_flyer},
                                            {"casas_texto", houses.size() == 1 ? "1 propiedad" : std::to_string(houses.size()) + " propiedades"},
                                            {"titulo", doc["titulo_efectivo"]},
                                            {"descripcion", doc["descripcion_efectiva"].get<std::string>().empty()
                                                                ? Json(nullptr)
                                                                : doc["descripcion_efectiva"]}}},
                        {"casas", houses},
                        {"portada", cover},
                        {"tema_css", theme_css},
                        {"contacto", has_contact ? contact : Json(nullptr)},
                        {"formato", nullptr},
                        {"modo", exporting ? "export" : "preview"},
                        {"generado_en", now_text()}};
    return snap;
}

Json DocumentContextBuilder::for_format(const Json& snapshot_context, const DocFormat& format) {
    Json ctx = snapshot_context;
    ctx["formato"] = Json{{"id", format.id},
                          {"nombre", format.name},
                          {"ancho", format.width},
                          {"alto", format.height},
                          {"max_badges", format.max_badges},
                          {"lineas_descripcion", format.description_lines}};
    for (Json& house : ctx["casas"]) {
        Json& badges = house["badges"];
        const std::size_t total = badges.size();
        const std::size_t keep = static_cast<std::size_t>(std::max(0, format.max_badges));
        if (total > keep) {
            badges.erase(badges.begin() + static_cast<std::ptrdiff_t>(keep), badges.end());
            house["badges_restantes"] = static_cast<int>(total - keep);
        } else {
            house["badges_restantes"] = 0;
        }
    }
    return ctx;
}

}  // namespace wcatalog
