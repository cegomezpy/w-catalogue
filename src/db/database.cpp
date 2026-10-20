#include "db/database.hpp"

#include <utility>

#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

// ---- Statement ----

Statement::Statement(Database& db, const std::string& sql) : db_(&db), lock_(db.mutex()) {
    if (sqlite3_prepare_v2(db.handle(), sql.c_str(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        db.throw_error("No se pudo preparar la sentencia");
    }
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_), lock_(std::move(other.lock_)) {
    other.stmt_ = nullptr;
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement& Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) db_->throw_error("bind");
    return *this;
}

Statement& Statement::bind(int index, double value) {
    if (sqlite3_bind_double(stmt_, index, value) != SQLITE_OK) db_->throw_error("bind");
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
        SQLITE_OK) {
        db_->throw_error("bind");
    }
    return *this;
}

Statement& Statement::bind_null(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) db_->throw_error("bind");
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    db_->throw_error("Error al ejecutar la sentencia");
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::is_null(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }

std::int64_t Statement::column_int(int col) const { return sqlite3_column_int64(stmt_, col); }

double Statement::column_double(int col) const { return sqlite3_column_double(stmt_, col); }

std::string Statement::column_text(int col) const {
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    if (!text) return {};
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(sqlite3_column_bytes(stmt_, col)));
}

// ---- Database ----

Database::Database(const fs::path& file) {
    // sqlite3_open_v2 espera la ruta en UTF-8
    int rc = sqlite3_open_v2(to_utf8(file).c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "sin memoria";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("No se pudo abrir la base de datos " + to_utf8(file) + ": " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = ON");
}

Database::~Database() { close(); }

void Database::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::throw_error(const std::string& context) {
    throw DbError(context + ": " + (db_ ? sqlite3_errmsg(db_) : "base cerrada"));
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "error desconocido";
        sqlite3_free(err);
        throw DbError("Error SQL: " + msg);
    }
}

Statement Database::prepare(const std::string& sql) { return Statement(*this, sql); }

std::int64_t Database::last_insert_rowid() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sqlite3_last_insert_rowid(db_);
}

int Database::changes() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sqlite3_changes(db_);
}

int Database::user_version() {
    Statement st = prepare("PRAGMA user_version");
    st.step();
    return static_cast<int>(st.column_int(0));
}

void Database::set_user_version(int version) {
    // PRAGMA no admite parámetros enlazados
    exec("PRAGMA user_version = " + std::to_string(version));
}

void Database::enable_wal() {
    Statement st = prepare("PRAGMA journal_mode = WAL");
    st.step();
}

void Database::backup_to(const fs::path& dest) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3* out = nullptr;
    if (sqlite3_open_v2(to_utf8(dest).c_str(), &out, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        std::string msg = out ? sqlite3_errmsg(out) : "sin memoria";
        if (out) sqlite3_close(out);
        throw DbError("No se pudo crear la copia de seguridad " + to_utf8(dest) + ": " + msg);
    }
    sqlite3_backup* bk = sqlite3_backup_init(out, "main", db_, "main");
    if (!bk) {
        std::string msg = sqlite3_errmsg(out);
        sqlite3_close(out);
        throw DbError("No se pudo iniciar la copia de seguridad: " + msg);
    }
    int rc = sqlite3_backup_step(bk, -1);
    sqlite3_backup_finish(bk);
    std::string msg = sqlite3_errmsg(out);
    sqlite3_close(out);
    if (rc != SQLITE_DONE) throw DbError("Falló la copia de seguridad: " + msg);
}

// ---- Transaction ----

Transaction::Transaction(Database& db) : db_(db), lock_(db.mutex()) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
    if (!done_) {
        try {
            db_.exec("ROLLBACK");
        } catch (...) {
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

}  // namespace wcatalog
