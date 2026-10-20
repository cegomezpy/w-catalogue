#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wcatalog {

// Instancia única por carpeta de datos: un mutex con nombre derivado de la ruta de data/,
// y data/.instance con el puerto de la instancia activa.
class SingleInstance {
public:
    SingleInstance() = default;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    ~SingleInstance();

    // true si esta es la primera instancia; false si ya hay otra en ejecución.
    bool acquire(const std::filesystem::path& data_dir);

    // Publica el puerto en data/.instance (solo la instancia activa).
    void publish_port(int port);

    // Puerto de la instancia activa. Espera hasta `wait` a que lo publique (por si
    // arrancó justo ahora).
    static std::optional<int> read_port(const std::filesystem::path& data_dir,
                                        std::chrono::milliseconds wait = std::chrono::milliseconds(0));

    static std::string mutex_name(const std::filesystem::path& data_dir);

private:
    HANDLE mutex_ = nullptr;
    std::filesystem::path instance_file_;
};

}  // namespace wcatalog
