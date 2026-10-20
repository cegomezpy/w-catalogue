#include "http/routes_theme.hpp"

#include <algorithm>
#include <cctype>

#include "http/json_response.hpp"
#include "settings/theme.hpp"

namespace wcatalog {

bool is_safe_https_url(const std::string& url) {
    static const std::string scheme = "https://";
    if (url.size() <= scheme.size() || url.size() > 2048) return false;
    for (size_t i = 0; i < scheme.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(url[i])) != scheme[i]) return false;
    }
    // el anfitrión no puede estar vacío ni empezar con símbolos
    const char first = url[scheme.size()];
    if (!std::isalnum(static_cast<unsigned char>(first))) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7F || c == '"' || c == '<' || c == '>' || c == '\\' || c == '^' || c == '`' ||
               c == '{' || c == '}' || c == '|';
    });
}

void register_theme_routes(httplib::Server& server, const SettingsService& settings, const FontLibrary& fonts) {
    server.Get("/api/tema.css", [&settings, &fonts](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(build_theme_css(settings, fonts, "/media/fuentes/", true), "text/css; charset=utf-8");
    });
}

void register_system_routes(httplib::Server& server, UrlOpener opener) {
    server.Post("/api/sistema/abrir-enlace", [opener = std::move(opener)](const httplib::Request& req, httplib::Response& res) {
        Json body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object() || !body.contains("url") || !body["url"].is_string()) {
            send_bad_request(res, "Falta la URL");
            return;
        }
        const std::string url = body["url"].get<std::string>();
        if (!is_safe_https_url(url)) {
            send_bad_request(res, "Solo se pueden abrir enlaces https://");
            return;
        }
        if (!opener || !opener(url)) {
            send_internal_error(res, "No se pudo abrir el navegador");
            return;
        }
        send_json(res, Json{{"status", "ok"}});
    });
}

}  // namespace wcatalog
