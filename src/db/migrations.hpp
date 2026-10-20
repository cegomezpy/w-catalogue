#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "db/database.hpp"

namespace wcatalog {

struct Migration {
    int version;      // consecutivas desde 1
    const char* sql;  // puede contener varias sentencias
};

// La base tiene un esquema más nuevo que el que conoce esta versión de la app.
struct SchemaTooNewError : DbError {
    using DbError::DbError;
};

// Una migración falló y se revirtió.
struct MigrationError : DbError {
    using DbError::DbError;
};

// Migraciones de la aplicación, en orden. Cada change nuevo añade la siguiente.
const std::vector<Migration>& app_migrations();

int latest_version(const std::vector<Migration>& migrations);

// Aplica las migraciones pendientes, cada una en su transacción.
// - Si la base es de una versión mayor: lanza SchemaTooNewError sin modificar nada.
// - Si la base ya tiene datos (versión > 0) y hay migraciones pendientes: copia de
//   seguridad en backups_dir antes de empezar.
// - Si una migración falla: se revierte, y se lanza MigrationError.
void migrate(Database& db, const std::vector<Migration>& migrations, const std::filesystem::path& backups_dir);

// Conserva solo los `keep` respaldos más recientes (catalog-v*.db) de la carpeta.
void prune_backups(const std::filesystem::path& backups_dir, std::size_t keep = 5);

// Abre catalog.db en data_dir, migra y activa WAL.
std::unique_ptr<Database> open_app_database(const std::filesystem::path& file,
                                            const std::filesystem::path& backups_dir);

}  // namespace wcatalog
