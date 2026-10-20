#pragma once

#include <functional>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "db/database.hpp"
#include "domain/errors.hpp"

namespace wcatalog {

// Ajustes del catálogo como clave-valor tipado. Las claves planas son "grupo.nombre"
// (p. ej. "tema.colorPrimario"); la API las expone anidadas por grupo.
class SettingsService {
public:
    explicit SettingsService(Database& db);

    // Comprueba que una familia elegida existe en la biblioteca. Si no se define, no se comprueba.
    void set_font_validator(std::function<bool(const std::string&)> validator);

    // Valores guardados combinados con los valores por defecto, anidados por grupo.
    nlohmann::json get() const;

    // Valor vigente de una clave plana (guardado o por defecto).
    std::string value(const std::string& key) const;

    static const char* default_value(const std::string& key);

    // Valida el parche (objeto anidado, parcial) y lo guarda en una sola transacción.
    // Lanza ValidationFailed sin escribir nada si algún campo no es válido.
    nlohmann::json update(const nlohmann::json& patch);

    // Restablece una clave a su valor por defecto. Usar dentro de una transacción del llamador.
    void reset(const std::string& key);

    static constexpr const char* kFuenteTitulos = "tipografia.fuenteTitulos";
    static constexpr const char* kFuenteTexto = "tipografia.fuenteTexto";

private:
    std::map<std::string, std::string> stored() const;

    Database& db_;
    std::function<bool(const std::string&)> font_exists_;
};

}  // namespace wcatalog
