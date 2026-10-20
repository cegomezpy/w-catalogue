#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <set>

#include "db/database.hpp"
#include "db/migrations.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;
using namespace wcatalog;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / name;
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::set<std::string> table_names(Database& db) {
    std::set<std::string> names;
    Statement st = db.prepare("SELECT name FROM sqlite_master WHERE type = 'table'");
    while (st.step()) names.insert(st.column_text(0));
    return names;
}

std::size_t count_backups(const fs::path& dir) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".db") ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("Database: sentencias preparadas, parametros y texto UTF-8") {
    TempDir tmp("wcatalog-test-db1");
    Database db(tmp.path / "t.db");
    db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, nombre TEXT, precio REAL)");

    Statement ins = db.prepare("INSERT INTO t (nombre, precio) VALUES (?, ?)");
    ins.bind(1, std::string("Casa en Medell\xC3\xADn \xC3\xB1")).bind(2, 1234.5);
    ins.step();
    CHECK(db.last_insert_rowid() == 1);
    CHECK(db.changes() == 1);

    Statement sel = db.prepare("SELECT nombre, precio FROM t WHERE id = ?");
    sel.bind(1, std::int64_t{1});
    REQUIRE(sel.step());
    CHECK(sel.column_text(0) == "Casa en Medell\xC3\xADn \xC3\xB1");
    CHECK(sel.column_double(1) == doctest::Approx(1234.5));
}

TEST_CASE("Database: claves foraneas activas y SQL invalido lanza DbError") {
    TempDir tmp("wcatalog-test-db2");
    Database db(tmp.path / "t.db");
    db.exec("CREATE TABLE a (id INTEGER PRIMARY KEY)");
    db.exec("CREATE TABLE b (id INTEGER PRIMARY KEY, a_id INTEGER REFERENCES a(id))");
    CHECK_THROWS_AS(db.exec("INSERT INTO b (a_id) VALUES (99)"), DbError);
    CHECK_THROWS_AS(db.exec("SELEKT nada"), DbError);
}

TEST_CASE("Database: la transaccion se revierte si no se confirma") {
    TempDir tmp("wcatalog-test-db3");
    Database db(tmp.path / "t.db");
    db.exec("CREATE TABLE t (x INTEGER)");
    {
        Transaction tx(db);
        db.exec("INSERT INTO t VALUES (1)");
    }
    {
        Statement st = db.prepare("SELECT COUNT(*) FROM t");
        st.step();
        CHECK(st.column_int(0) == 0);
    }
    {
        Transaction tx(db);
        db.exec("INSERT INTO t VALUES (2)");
        tx.commit();
    }
    Statement st = db.prepare("SELECT COUNT(*) FROM t");
    st.step();
    CHECK(st.column_int(0) == 1);
}

TEST_CASE("Database: activa WAL") {
    TempDir tmp("wcatalog-test-db4");
    Database db(tmp.path / "t.db");
    db.enable_wal();
    Statement st = db.prepare("PRAGMA journal_mode");
    st.step();
    CHECK(st.column_text(0) == "wal");
}

TEST_CASE("migrate: base nueva aplica la migracion 1 sin copia de seguridad") {
    TempDir tmp("wcatalog-test-mig1");
    Database db(tmp.path / "c.db");
    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() == latest_version(app_migrations()));
    CHECK(table_names(db).count("app_meta") == 1);
    CHECK_FALSE(fs::exists(tmp.path / "backups"));
}

TEST_CASE("migrate: base ya migrada no hace nada") {
    TempDir tmp("wcatalog-test-mig2");
    Database db(tmp.path / "c.db");
    migrate(db, app_migrations(), tmp.path / "backups");
    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() == latest_version(app_migrations()));
    CHECK_FALSE(fs::exists(tmp.path / "backups"));
}

TEST_CASE("migrate: una migracion fallida se revierte y se informa") {
    TempDir tmp("wcatalog-test-mig3");
    Database db(tmp.path / "c.db");
    std::vector<Migration> migs = {
        {1, "CREATE TABLE a (x INTEGER);"},
        {2, "CREATE TABLE b (x INTEGER); INSERT INTO tabla_inexistente VALUES (1);"},
    };
    CHECK_THROWS_AS(migrate(db, migs, tmp.path / "backups"), MigrationError);
    CHECK(db.user_version() == 1);
    auto names = table_names(db);
    CHECK(names.count("a") == 1);
    CHECK(names.count("b") == 0);  // la parte buena de la migracion 2 tambien se revirtio
}

TEST_CASE("migrate: crea copia de seguridad antes de migrar una base existente") {
    TempDir tmp("wcatalog-test-mig4");
    Database db(tmp.path / "c.db");
    std::vector<Migration> v1 = {{1, "CREATE TABLE a (x INTEGER); INSERT INTO a VALUES (7);"}};
    migrate(db, v1, tmp.path / "backups");
    CHECK_FALSE(fs::exists(tmp.path / "backups"));

    std::vector<Migration> v2 = v1;
    v2.push_back({2, "CREATE TABLE b (x INTEGER);"});
    migrate(db, v2, tmp.path / "backups");
    CHECK(db.user_version() == 2);
    REQUIRE(count_backups(tmp.path / "backups") == 1);

    // la copia conserva el estado previo (version 1, con datos y sin la tabla b)
    for (const auto& e : fs::directory_iterator(tmp.path / "backups")) {
        CHECK(to_utf8(e.path().filename()).rfind("catalog-v1-", 0) == 0);
        Database bk(e.path());
        CHECK(bk.user_version() == 1);
        CHECK(table_names(bk).count("b") == 0);
        Statement st = bk.prepare("SELECT x FROM a");
        REQUIRE(st.step());
        CHECK(st.column_int(0) == 7);
    }
}

TEST_CASE("migrate: una base del futuro se rechaza sin modificarla") {
    TempDir tmp("wcatalog-test-mig5");
    fs::path file = tmp.path / "c.db";
    {
        Database db(file);
        db.exec("CREATE TABLE a (x INTEGER)");
        db.set_user_version(99);
    }
    auto before = fs::file_size(file);
    std::string content_before;
    {
        std::ifstream in(file, std::ios::binary);
        content_before.assign(std::istreambuf_iterator<char>(in), {});
    }
    {
        Database db(file);
        CHECK_THROWS_AS(migrate(db, app_migrations(), tmp.path / "backups"), SchemaTooNewError);
    }
    std::string content_after;
    {
        std::ifstream in(file, std::ios::binary);
        content_after.assign(std::istreambuf_iterator<char>(in), {});
    }
    CHECK(fs::file_size(file) == before);
    CHECK(content_after == content_before);
    CHECK_FALSE(fs::exists(tmp.path / "backups"));
}

TEST_CASE("prune_backups conserva solo los 5 mas recientes") {
    TempDir tmp("wcatalog-test-prune");
    auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 8; ++i) {
        fs::path f = tmp.path / ("catalog-v1-2026010" + std::to_string(i) + ".db");
        std::ofstream(f) << "x";
        fs::last_write_time(f, now - std::chrono::hours(100 - i));  // i mayor = mas reciente
    }
    std::ofstream(tmp.path / "otro.txt") << "x";
    prune_backups(tmp.path, 5);
    CHECK(count_backups(tmp.path) == 5);
    CHECK_FALSE(fs::exists(tmp.path / "catalog-v1-20260100.db"));
    CHECK(fs::exists(tmp.path / "catalog-v1-20260107.db"));
    CHECK(fs::exists(tmp.path / "otro.txt"));
}

TEST_CASE("open_app_database migra, activa WAL y deja la version actual") {
    TempDir tmp("wcatalog-test-open");
    auto db = open_app_database(tmp.path / "catalog.db", tmp.path / "backups");
    CHECK(db->user_version() == latest_version(app_migrations()));
    Statement st = db->prepare("PRAGMA journal_mode");
    st.step();
    CHECK(st.column_text(0) == "wal");
}

TEST_CASE("migracion 2: crea ajustes y fuente y conserva app_meta") {
    TempDir tmp("wcatalog-test-mig-v2");
    Database db(tmp.path / "c.db");
    std::vector<Migration> only1 = {app_migrations()[0]};
    migrate(db, only1, tmp.path / "backups");
    REQUIRE(db.user_version() == 1);

    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() >= 2);
    auto names = table_names(db);
    CHECK(names.count("app_meta") == 1);
    CHECK(names.count("ajustes") == 1);
    CHECK(names.count("fuente") == 1);
    Statement st = db.prepare("SELECT COUNT(*) FROM app_meta WHERE clave = 'creada_en'");
    st.step();
    CHECK(st.column_int(0) == 1);
}

TEST_CASE("migracion 3: se aplica sobre ajustes existentes y siembra las 16 predefinidas") {
    TempDir tmp("wcatalog-test-mig-v3");
    Database db(tmp.path / "c.db");
    std::vector<Migration> upto2 = {app_migrations()[0], app_migrations()[1]};
    migrate(db, upto2, tmp.path / "backups");
    db.exec("INSERT INTO ajustes (clave, valor) VALUES ('general.moneda', 'EUR')");

    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() >= 3);
    auto names = table_names(db);
    for (const char* t : {"casa", "caracteristica", "casa_caracteristica", "ajustes", "app_meta"}) CHECK(names.count(t) == 1);
    {
        Statement st = db.prepare("SELECT valor FROM ajustes WHERE clave = 'general.moneda'");
        REQUIRE(st.step());
        CHECK(st.column_text(0) == "EUR");
    }
    // se hizo copia de seguridad de la base v2 antes de migrar
    CHECK(count_backups(tmp.path / "backups") == 1);

    {
        Statement st = db.prepare("SELECT COUNT(*), SUM(predefinida), COUNT(DISTINCT clave) FROM caracteristica");
        st.step();
        CHECK(st.column_int(0) == 16);
        CHECK(st.column_int(1) == 16);
        CHECK(st.column_int(2) == 16);
    }
    for (const char* k : {"gas_permanente", "respaldo_energetico", "internet", "seguridad", "agua", "climatizacion",
                          "patio_jardin", "terraza", "balcon", "parqueos", "cuarto_servicio", "lavanderia", "piscina",
                          "amueblada", "estado_conservacion", "anio_construccion"}) {
        Statement st = db.prepare("SELECT predefinida FROM caracteristica WHERE clave = ?");
        st.bind(1, std::string(k));
        REQUIRE_MESSAGE(st.step(), k);
        CHECK(st.column_int(0) == 1);
    }
    Statement st = db.prepare("SELECT badge_visible FROM caracteristica WHERE clave = 'anio_construccion'");
    st.step();
    CHECK(st.column_int(0) == 0);
    Statement st2 = db.prepare("SELECT COUNT(*) FROM caracteristica WHERE badge_visible = 1");
    st2.step();
    CHECK(st2.column_int(0) == 15);
}

TEST_CASE("migracion 3: las restricciones CHECK y las cascadas funcionan") {
    TempDir tmp("wcatalog-test-mig-v3b");
    auto db = open_app_database(tmp.path / "c.db", tmp.path / "backups");
    auto ts = "'2026-01-01T00:00:00Z'";
    auto ins = [&](const std::string& cols_vals) {
        db->exec("INSERT INTO casa (titulo, creado, actualizado" + cols_vals + ")");
    };
    CHECK_NOTHROW(db->exec(std::string("INSERT INTO casa (titulo, creado, actualizado) VALUES ('A', ") + ts + "," + ts + ")"));
    CHECK_THROWS_AS(db->exec(std::string("INSERT INTO casa (titulo, creado, actualizado) VALUES ('  ', ") + ts + "," + ts + ")"), DbError);
    CHECK_THROWS_AS(db->exec(std::string("INSERT INTO casa (titulo, estado, creado, actualizado) VALUES ('B', 'rota', ") + ts + "," + ts + ")"), DbError);
    CHECK_THROWS_AS(db->exec(std::string("INSERT INTO casa (titulo, cuartos, creado, actualizado) VALUES ('B', -1, ") + ts + "," + ts + ")"), DbError);
    CHECK_THROWS_AS(db->exec(std::string("INSERT INTO casa (titulo, lat, creado, actualizado) VALUES ('B', 10, ") + ts + "," + ts + ")"), DbError);
    CHECK_THROWS_AS(db->exec(std::string("INSERT INTO casa (titulo, lat, lon, creado, actualizado) VALUES ('B', 95, 0, ") + ts + "," + ts + ")"), DbError);
    (void)ins;

    db->exec("INSERT INTO casa_caracteristica (casa_id, caracteristica_id, valor) VALUES (1, 1, '1')");
    CHECK_THROWS_AS(db->exec("INSERT INTO casa_caracteristica (casa_id, caracteristica_id, valor) VALUES (99, 1, '1')"), DbError);
    db->exec("DELETE FROM casa WHERE id = 1");
    Statement st = db->prepare("SELECT COUNT(*) FROM casa_caracteristica");
    st.step();
    CHECK(st.column_int(0) == 0);
}

TEST_CASE("migracion 5: documentos se crea sobre una base con casas y fotos y respeta las cascadas") {
    TempDir tmp("wcatalog-test-mig-v5");
    Database db(tmp.path / "c.db");
    std::vector<Migration> upto4(app_migrations().begin(), app_migrations().begin() + 4);
    migrate(db, upto4, tmp.path / "backups");
    REQUIRE(db.user_version() == 4);
    const std::string ts = "'2026-01-01T00:00:00Z'";
    db.exec("INSERT INTO casa (titulo, creado, actualizado) VALUES ('A', " + ts + "," + ts + ")");
    db.exec("INSERT INTO casa (titulo, creado, actualizado) VALUES ('B', " + ts + "," + ts + ")");
    db.exec("INSERT INTO foto (casa_id, orden, formato_original, ancho, alto, bytes_original) VALUES (1, 1, 'jpg', 10, 10, 5)");

    migrate(db, app_migrations(), tmp.path / "backups");
    CHECK(db.user_version() >= 5);
    auto names = table_names(db);
    CHECK(names.count("documento") == 1);
    CHECK(names.count("documento_casa") == 1);
    {
        Statement st = db.prepare("SELECT COUNT(*) FROM foto");
        st.step();
        CHECK(st.column_int(0) == 1);  // la foto existente se conserva
    }

    db.exec("INSERT INTO documento (tipo, portada_foto_id, creado, actualizado) VALUES ('catalogo', 1, " + ts + "," + ts + ")");
    db.exec("INSERT INTO documento_casa (documento_id, casa_id, orden) VALUES (1, 1, 1)");
    db.exec("INSERT INTO documento_casa (documento_id, casa_id, orden) VALUES (1, 2, 2)");
    CHECK_THROWS_AS(db.exec("INSERT INTO documento (tipo, creado, actualizado) VALUES ('otro', " + ts + "," + ts + ")"), DbError);
    CHECK_THROWS_AS(db.exec("INSERT INTO documento_casa (documento_id, casa_id, orden) VALUES (1, 99, 3)"), DbError);

    // borrar la foto de portada deja el documento sin portada (SET NULL)
    db.exec("DELETE FROM foto WHERE id = 1");
    {
        Statement st = db.prepare("SELECT portada_foto_id FROM documento WHERE id = 1");
        REQUIRE(st.step());
        CHECK(st.is_null(0));
    }
    // borrar una casa la quita del documento (CASCADE)
    db.exec("DELETE FROM casa WHERE id = 2");
    Statement st = db.prepare("SELECT COUNT(*) FROM documento_casa WHERE documento_id = 1");
    st.step();
    CHECK(st.column_int(0) == 1);
    // borrar el documento borra sus filas
    db.exec("DELETE FROM documento WHERE id = 1");
    Statement st2 = db.prepare("SELECT COUNT(*) FROM documento_casa");
    st2.step();
    CHECK(st2.column_int(0) == 0);
}
