#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>

namespace wcatalog {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Database;

// Sentencia preparada. Mantiene bloqueada la conexión mientras vive.
class Statement {
public:
    Statement(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, double value);
    Statement& bind(int index, const std::string& value);
    Statement& bind_null(int index);

    // true si hay una fila disponible, false al terminar.
    bool step();
    void reset();

    bool is_null(int col) const;
    std::int64_t column_int(int col) const;
    double column_double(int col) const;
    std::string column_text(int col) const;

private:
    friend class Database;
    Statement(Database& db, const std::string& sql);

    Database* db_;
    sqlite3_stmt* stmt_ = nullptr;
    std::unique_lock<std::recursive_mutex> lock_;
};

// Conexión única a SQLite protegida por mutex (recursivo, para anidar transacciones y sentencias).
class Database {
public:
    explicit Database(const std::filesystem::path& file);
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);

    std::int64_t last_insert_rowid();
    int changes();

    int user_version();
    void set_user_version(int version);

    // Activa el modo WAL. Se llama aparte para no tocar una base que se va a rechazar.
    void enable_wal();

    // Copia consistente de la base abierta a otro archivo (sqlite3_backup).
    void backup_to(const std::filesystem::path& dest);

    void close();

    sqlite3* handle() { return db_; }
    std::recursive_mutex& mutex() { return mutex_; }

private:
    friend class Statement;
    [[noreturn]] void throw_error(const std::string& context);

    sqlite3* db_ = nullptr;
    std::recursive_mutex mutex_;
};

// Transacción RAII: si no se confirma con commit(), se revierte al destruirse.
class Transaction {
public:
    explicit Transaction(Database& db);
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    void commit();

private:
    Database& db_;
    std::unique_lock<std::recursive_mutex> lock_;
    bool done_ = false;
};

}  // namespace wcatalog
