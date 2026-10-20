#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "runtime/log.hpp"

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

fs::path fresh_dir(const char* name) {
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("Logger escribe lineas con nivel y mensaje") {
    fs::path dir = fresh_dir("wcatalog-test-log1");
    wcatalog::Logger lg;
    lg.open(dir / "app.log");
    lg.info("hola");
    lg.error("fallo con \xC3\xB1");
    lg.close();
    std::string text = slurp(dir / "app.log");
    CHECK(text.find("[INFO] hola") != std::string::npos);
    CHECK(text.find("[ERROR] fallo con \xC3\xB1") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Logger rota al superar el tamano y conserva el archivo anterior") {
    fs::path dir = fresh_dir("wcatalog-test-log2");
    wcatalog::Logger lg;
    lg.open(dir / "app.log", 200);
    for (int i = 0; i < 20; ++i) lg.info("linea numero " + std::to_string(i));
    lg.close();

    REQUIRE(fs::exists(dir / "app.log.1"));
    CHECK(slurp(dir / "app.log.1").find("linea numero") != std::string::npos);
    CHECK(fs::file_size(dir / "app.log") <= 200 + 100);
    CHECK(slurp(dir / "app.log").find("linea numero 19") != std::string::npos);
    fs::remove_all(dir);
}
