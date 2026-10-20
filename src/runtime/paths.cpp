#include "runtime/paths.hpp"

#include <windows.h>

#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace wcatalog {

fs::path current_exe_dir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) throw std::runtime_error("No se pudo obtener la ruta del ejecutable");
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf).parent_path();
}

AppPaths make_paths(const fs::path& exe_dir) {
    AppPaths p;
    p.exe_dir = exe_dir;
    p.data_dir = exe_dir / "data";
    p.logs_dir = p.data_dir / "logs";
    p.backups_dir = p.data_dir / "backups";
    p.fonts_dir = p.data_dir / "fuentes";
    p.photos_dir = p.data_dir / "fotos";
    p.tmp_dir = p.data_dir / "tmp";
    return p;
}

fs::path resolve_web_dir(const fs::path& exe_dir) {
    std::error_code ec;
    fs::path beside = exe_dir / "web";
    if (fs::is_directory(beside, ec)) return beside;
    fs::path dev = exe_dir.parent_path() / "web";
    if (fs::is_directory(dev, ec)) return dev;
    return beside;
}

fs::path resolve_included_fonts_dir(const fs::path& exe_dir) {
    std::error_code ec;
    fs::path beside = exe_dir / "fonts";
    if (fs::is_directory(beside, ec)) return beside;
    fs::path dev = exe_dir.parent_path() / "fonts";
    if (fs::is_directory(dev, ec)) return dev;
    return beside;
}

std::string to_utf8(const fs::path& p) {
    return p.u8string();
}

namespace {

void ensure_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir)) {
        throw std::runtime_error("No se pudo crear la carpeta: " + to_utf8(dir));
    }
}

}  // namespace

void ensure_data_dirs(const AppPaths& paths) {
    ensure_dir(paths.data_dir);
    ensure_dir(paths.logs_dir);
    ensure_dir(paths.backups_dir);
    ensure_dir(paths.fonts_dir);
    ensure_dir(paths.photos_dir);
    ensure_dir(paths.tmp_dir);

    // Los permisos de carpeta no bastan para saber si se puede escribir: se prueba.
    const fs::path probe = paths.data_dir / ".write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("No se puede escribir en la carpeta de datos: " +
                                     to_utf8(paths.data_dir));
        }
        out << "x";
    }
    std::error_code ec;
    fs::remove(probe, ec);
}

}  // namespace wcatalog
