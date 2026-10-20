#include "db/migrations.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "db/migration_sql.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

const std::vector<Migration>& app_migrations() {
    static const std::vector<Migration> migrations = {
        {1,
         "CREATE TABLE app_meta ("
         "  clave TEXT PRIMARY KEY,"
         "  valor TEXT NOT NULL"
         ");"
         "INSERT INTO app_meta (clave, valor) VALUES ('creada_en', strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));"},
        {2,
         "CREATE TABLE ajustes ("
         "  clave TEXT PRIMARY KEY,"
         "  valor TEXT NOT NULL"
         ");"
         "CREATE TABLE fuente ("
         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  familia TEXT NOT NULL,"
         "  peso INTEGER NOT NULL,"
         "  estilo TEXT NOT NULL CHECK (estilo IN ('normal', 'italic')),"
         "  archivo TEXT NOT NULL,"
         "  formato TEXT NOT NULL CHECK (formato IN ('ttf', 'otf')),"
         "  creado TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),"
         "  UNIQUE (familia, peso, estilo)"
         ");"},
        {3, kMigrationHouses},
        {4,
         "CREATE TABLE foto ("
         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  casa_id INTEGER NOT NULL REFERENCES casa (id) ON DELETE CASCADE,"
         "  orden INTEGER NOT NULL,"
         "  pie TEXT NOT NULL DEFAULT '' CHECK (length(pie) <= 120),"
         "  formato_original TEXT NOT NULL CHECK (formato_original IN ('jpg', 'png')),"
         "  ancho INTEGER NOT NULL CHECK (ancho > 0),"
         "  alto INTEGER NOT NULL CHECK (alto > 0),"
         "  bytes_original INTEGER NOT NULL CHECK (bytes_original > 0),"
         "  creado TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))"
         ");"
         "CREATE INDEX idx_foto_casa_orden ON foto (casa_id, orden);"},
    };
    return migrations;
}

int latest_version(const std::vector<Migration>& migrations) {
    return migrations.empty() ? 0 : migrations.back().version;
}

namespace {

std::string stamp_now() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return os.str();
}

fs::path make_backup(Database& db, int from_version, const fs::path& backups_dir) {
    std::error_code ec;
    fs::create_directories(backups_dir, ec);

    std::string base = "catalog-v" + std::to_string(from_version) + "-" + stamp_now();
    fs::path dest = backups_dir / (base + ".db");
    for (int n = 2; fs::exists(dest); ++n) dest = backups_dir / (base + "-" + std::to_string(n) + ".db");

    db.backup_to(dest);
    log().info("Copia de seguridad creada: " + to_utf8(dest));
    prune_backups(backups_dir);
    return dest;
}

}  // namespace

void prune_backups(const fs::path& backups_dir, std::size_t keep) {
    std::error_code ec;
    if (!fs::is_directory(backups_dir, ec)) return;

    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(backups_dir, ec)) {
        const std::string name = to_utf8(entry.path().filename());
        if (entry.is_regular_file() && name.rfind("catalog-v", 0) == 0 && entry.path().extension() == ".db") {
            files.emplace_back(entry.last_write_time(), entry.path());
        }
    }
    if (files.size() <= keep) return;

    // más recientes primero; el nombre desempata si las marcas de tiempo coinciden
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second.filename() > b.second.filename();
    });
    for (std::size_t i = keep; i < files.size(); ++i) {
        fs::remove(files[i].second, ec);
        // los -wal/-shm de un respaldo no existen, pero se limpian por si acaso
        fs::remove(fs::path(files[i].second) += "-wal", ec);
        fs::remove(fs::path(files[i].second) += "-shm", ec);
    }
}

void migrate(Database& db, const std::vector<Migration>& migrations, const fs::path& backups_dir) {
    const int current = db.user_version();
    const int target = latest_version(migrations);

    if (current > target) {
        throw SchemaTooNewError("La base de datos es de una versión más nueva (esquema " + std::to_string(current) +
                                ") que la que conoce esta aplicación (esquema " + std::to_string(target) +
                                "). Use una versión más reciente de WCatalog.");
    }
    if (current == target) return;

    if (current > 0) make_backup(db, current, backups_dir);

    for (const Migration& m : migrations) {
        if (m.version <= current) continue;
        try {
            Transaction tx(db);
            db.exec(m.sql);
            db.set_user_version(m.version);
            tx.commit();
            log().info("Migración " + std::to_string(m.version) + " aplicada");
        } catch (const DbError& e) {
            log().error("Migración " + std::to_string(m.version) + " falló: " + e.what());
            throw MigrationError("La migración " + std::to_string(m.version) + " de la base de datos falló: " +
                                 e.what());
        }
    }
}

std::unique_ptr<Database> open_app_database(const fs::path& file, const fs::path& backups_dir) {
    auto db = std::make_unique<Database>(file);
    migrate(*db, app_migrations(), backups_dir);
    db->enable_wal();
    return db;
}

}  // namespace wcatalog
