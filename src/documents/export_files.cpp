#include "documents/export_files.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#include "documents/document_text.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

std::string current_stamp() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d_%H%M");
    return os.str();
}

std::string output_folder_name(const std::string& stamp, const std::string& title, const std::string& type) {
    return stamp + "_" + slugify_title(title) + "_" + type;
}

fs::path create_output_folder(const fs::path& outputs_dir, const std::string& name) {
    std::error_code ec;
    fs::create_directories(outputs_dir, ec);
    if (ec) throw std::runtime_error("No se pudo crear la carpeta de salidas: " + to_utf8(outputs_dir));

    // create_directory devuelve false si ya existía: así dos exportaciones simultáneas no comparten carpeta
    for (int n = 1; n < 10000; ++n) {
        const fs::path candidate = outputs_dir / fs::u8path(n == 1 ? name : name + "-" + std::to_string(n));
        ec.clear();
        if (fs::create_directory(candidate, ec)) return candidate;
        if (ec) throw std::runtime_error("No se pudo crear la carpeta de la exportación: " + to_utf8(candidate));
    }
    throw std::runtime_error("No se pudo elegir un nombre libre para la carpeta de la exportación");
}

void copy_assets(const fs::path& folder, const std::vector<AssetFile>& assets) {
    std::set<std::string> done;
    for (const AssetFile& a : assets) {
        if (!done.insert(a.to).second) continue;
        const fs::path dest = folder / "assets" / fs::u8path(a.to);
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        if (ec) throw std::runtime_error("No se pudo crear la carpeta: " + to_utf8(dest.parent_path()));
        fs::copy_file(a.from, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("No se pudo copiar " + to_utf8(a.from) + " a la exportación: " + ec.message());
        }
    }
}

void write_text_file(const fs::path& file, const std::string& content) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw std::runtime_error("No se pudo escribir el archivo: " + to_utf8(file));
}

}  // namespace wcatalog
