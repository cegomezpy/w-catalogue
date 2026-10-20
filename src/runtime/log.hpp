#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace wcatalog {

// Log a archivo con rotación: al superar max_bytes, app.log pasa a app.log.1
// (sustituyendo al anterior) y se empieza uno nuevo.
class Logger {
public:
    Logger() = default;

    void open(const std::filesystem::path& file, std::uintmax_t max_bytes = 1024 * 1024);
    void close();

    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    void write(const char* level, const std::string& msg);
    void rotate_locked();

    std::mutex mutex_;
    std::filesystem::path file_;
    std::uintmax_t max_bytes_ = 0;
    std::ofstream out_;
};

// Logger global de la aplicación; si no se abrió, descarta los mensajes.
Logger& log();

}  // namespace wcatalog
