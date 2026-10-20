#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const std::wstring& name) {
        path = fs::temp_directory_path() / name;
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("make_paths cuelga data/ de la carpeta del exe") {
    AppPaths p = make_paths(fs::path(L"C:\\app"));
    CHECK(p.data_dir == fs::path(L"C:\\app") / "data");
    CHECK(p.logs_dir == p.data_dir / "logs");
    CHECK(p.backups_dir == p.data_dir / "backups");
}

TEST_CASE("ensure_data_dirs crea la estructura si falta") {
    TempDir tmp(L"wcatalog-test-paths");
    AppPaths p = make_paths(tmp.path);
    REQUIRE_FALSE(fs::exists(p.data_dir));
    ensure_data_dirs(p);
    CHECK(fs::is_directory(p.data_dir));
    CHECK(fs::is_directory(p.logs_dir));
    CHECK(fs::is_directory(p.backups_dir));
    CHECK_FALSE(fs::exists(p.data_dir / ".write-test"));
}

TEST_CASE("ensure_data_dirs funciona con tildes y ñ en la ruta") {
    // \u00f1 = ñ, \u00e1 = á, \u00f3 = ó, \u00fa = ú
    TempDir tmp(L"wcatalog-prueba-\u00f1and\u00fa-cami\u00f3n");
    AppPaths p = make_paths(tmp.path / L"Cat\u00e1logo a\u00f1o");
    ensure_data_dirs(p);
    CHECK(fs::is_directory(p.logs_dir));
    CHECK(to_utf8(p.data_dir).find("Cat\xC3\xA1logo a\xC3\xB1o") != std::string::npos);
}

TEST_CASE("ensure_data_dirs falla indicando la ruta si no se puede crear") {
    TempDir tmp(L"wcatalog-test-paths-fail");
    {
        // data ya existe como archivo: no se puede crear la carpeta
        std::ofstream f(tmp.path / "data");
        f << "x";
    }
    AppPaths p = make_paths(tmp.path);
    try {
        ensure_data_dirs(p);
        FAIL("debia lanzar una excepcion");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("data") != std::string::npos);
    }
}
