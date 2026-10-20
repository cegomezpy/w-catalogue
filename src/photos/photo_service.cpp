#include "photos/photo_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <random>
#include <regex>
#include <set>

#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

std::string random_name() {
    static std::mutex m;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(m);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(rng()));
    return buf;
}

void write_file(const fs::path& file, const std::uint8_t* data, std::size_t size) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error("No se pudo escribir el archivo: " + to_utf8(file));
}

// Quita la carpeta temporal al salir de ámbito, haya o no error.
struct TmpDirGuard {
    fs::path dir;
    ~TmpDirGuard() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

// Caracteres Unicode de un texto UTF-8 (cuenta los bytes que no son de continuación).
std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string url_for(std::int64_t house_id, std::int64_t photo_id, const char* kind) {
    return "/media/fotos/" + std::to_string(house_id) + "/" + std::to_string(photo_id) + "_" + kind + ".jpg";
}

}  // namespace

PhotoService::PhotoService(Database& db, PhotoPaths paths) : db_(db), paths_(std::move(paths)) {}

fs::path PhotoService::house_dir(std::int64_t house_id) const { return paths_.photos_dir / std::to_string(house_id); }

bool PhotoService::house_exists(std::int64_t id) const {
    Statement st = db_.prepare("SELECT 1 FROM casa WHERE id = ?");
    st.bind(1, id);
    return st.step();
}

int PhotoService::count_for(std::int64_t house_id) const {
    Statement st = db_.prepare("SELECT count(*) FROM foto WHERE casa_id = ?");
    st.bind(1, house_id);
    st.step();
    return static_cast<int>(st.column_int(0));
}

Json PhotoService::photo_json(std::int64_t house_id, std::int64_t id, std::int64_t order, const std::string& caption,
                              int w, int h) const {
    return Json{{"id", id},
                {"orden", order},
                {"pie", caption},
                {"ancho", w},
                {"alto", h},
                {"url_opt", url_for(house_id, id, "opt")},
                {"url_min", url_for(house_id, id, "min")}};
}

Json PhotoService::list(std::int64_t house_id) const {
    Json out = Json::array();
    Statement st = db_.prepare("SELECT id, orden, pie, ancho, alto FROM foto WHERE casa_id = ? ORDER BY orden, id");
    st.bind(1, house_id);
    while (st.step()) {
        out.push_back(photo_json(house_id, st.column_int(0), st.column_int(1), st.column_text(2),
                                 static_cast<int>(st.column_int(3)), static_cast<int>(st.column_int(4))));
    }
    return out;
}

Json PhotoService::upload(std::int64_t house_id, const std::uint8_t* data, std::size_t size) {
    if (!house_exists(house_id)) throw NotFoundError("La casa no existe");
    if (count_for(house_id) >= kMaxPhotosPerHouse) {
        throw ConflictError("La casa ya tiene el máximo de " + std::to_string(kMaxPhotosPerHouse) + " fotos");
    }

    // decodificar y redimensionar es lo pesado: se hace sin tener la base bloqueada
    const ProcessedPhoto processed = process_photo(data, size);

    std::error_code ec;
    fs::create_directories(paths_.tmp_dir, ec);
    TmpDirGuard tmp{paths_.tmp_dir / ("foto-" + random_name())};
    fs::create_directories(tmp.dir, ec);
    if (ec) throw std::runtime_error("No se pudo crear la carpeta temporal: " + to_utf8(tmp.dir));

    const std::string orig_name = "orig." + processed.original_ext;
    write_file(tmp.dir / orig_name, data, size);
    write_file(tmp.dir / "opt.jpg", processed.opt_jpeg.data(), processed.opt_jpeg.size());
    write_file(tmp.dir / "min.jpg", processed.min_jpeg.data(), processed.min_jpeg.size());

    const fs::path dest_dir = house_dir(house_id);
    std::int64_t photo_id = 0;
    std::int64_t order = 0;
    std::vector<fs::path> moved;
    bool made_dir = false;
    auto undo_moves = [&] {
        std::error_code e;
        for (const fs::path& f : moved) fs::remove(f, e);
        if (made_dir) fs::remove(dest_dir, e);  // solo si quedó vacía
    };

    try {
        Transaction tx(db_);
        if (!house_exists(house_id)) throw NotFoundError("La casa no existe");
        if (count_for(house_id) >= kMaxPhotosPerHouse) {
            throw ConflictError("La casa ya tiene el máximo de " + std::to_string(kMaxPhotosPerHouse) + " fotos");
        }
        {
            Statement mx = db_.prepare("SELECT COALESCE(MAX(orden), 0) FROM foto WHERE casa_id = ?");
            mx.bind(1, house_id);
            mx.step();
            order = mx.column_int(0) + 1;
        }
        {
            Statement ins = db_.prepare(
                "INSERT INTO foto (casa_id, orden, formato_original, ancho, alto, bytes_original) "
                "VALUES (?, ?, ?, ?, ?, ?)");
            ins.bind(1, house_id);
            ins.bind(2, order);
            ins.bind(3, processed.original_ext);
            ins.bind(4, static_cast<std::int64_t>(processed.width));
            ins.bind(5, static_cast<std::int64_t>(processed.height));
            ins.bind(6, static_cast<std::int64_t>(size));
            ins.step();
        }
        photo_id = db_.last_insert_rowid();

        made_dir = !fs::exists(dest_dir, ec);
        fs::create_directories(dest_dir, ec);
        if (ec) throw std::runtime_error("No se pudo crear la carpeta de fotos: " + to_utf8(dest_dir));
        const std::string prefix = std::to_string(photo_id) + "_";
        const std::pair<std::string, std::string> files[] = {
            {orig_name, prefix + orig_name},
            {"opt.jpg", prefix + "opt.jpg"},
            {"min.jpg", prefix + "min.jpg"},
        };
        for (const auto& [from, to] : files) {
            fs::rename(tmp.dir / from, dest_dir / to);
            moved.push_back(dest_dir / to);
        }
        tx.commit();
    } catch (...) {
        undo_moves();
        throw;
    }

    log().info("Foto " + std::to_string(photo_id) + " añadida a la casa " + std::to_string(house_id));
    return photo_json(house_id, photo_id, order, "", processed.width, processed.height);
}

Json PhotoService::reorder(std::int64_t house_id, const Json& ids) {
    if (!house_exists(house_id)) throw NotFoundError("La casa no existe");
    if (!ids.is_array()) throw ValidationFailed({{"ids", "Se esperaba la lista de ids de las fotos"}});

    std::vector<std::int64_t> wanted;
    for (const Json& v : ids) {
        if (!v.is_number_integer()) throw ValidationFailed({{"ids", "Cada id debe ser un número entero"}});
        wanted.push_back(v.get<std::int64_t>());
    }

    Transaction tx(db_);
    std::set<std::int64_t> current;
    {
        Statement st = db_.prepare("SELECT id FROM foto WHERE casa_id = ?");
        st.bind(1, house_id);
        while (st.step()) current.insert(st.column_int(0));
    }
    const std::set<std::int64_t> given(wanted.begin(), wanted.end());
    if (given.size() != wanted.size() || given != current) {
        throw ValidationFailed({{"ids", "La lista debe contener exactamente las fotos de la casa, sin repetir"}});
    }
    {
        Statement up = db_.prepare("UPDATE foto SET orden = ? WHERE id = ? AND casa_id = ?");
        for (std::size_t i = 0; i < wanted.size(); ++i) {
            up.reset();
            up.bind(1, static_cast<std::int64_t>(i) + 1);
            up.bind(2, wanted[i]);
            up.bind(3, house_id);
            up.step();
        }
    }
    tx.commit();
    return list(house_id);
}

Json PhotoService::set_caption(std::int64_t photo_id, const Json& payload) {
    if (!payload.contains("pie") || !payload["pie"].is_string()) {
        throw ValidationFailed({{"pie", "El pie de foto debe ser un texto"}});
    }
    const std::string caption = trim(payload["pie"].get<std::string>());
    if (utf8_length(caption) > static_cast<std::size_t>(kMaxCaptionChars)) {
        throw ValidationFailed(
            {{"pie", "El pie de foto admite como máximo " + std::to_string(kMaxCaptionChars) + " caracteres"}});
    }

    Transaction tx(db_);
    {
        Statement up = db_.prepare("UPDATE foto SET pie = ? WHERE id = ?");
        up.bind(1, caption);
        up.bind(2, photo_id);
        up.step();
    }
    if (db_.changes() == 0) throw NotFoundError("La foto no existe");
    tx.commit();

    Statement st = db_.prepare("SELECT casa_id, orden, pie, ancho, alto FROM foto WHERE id = ?");
    st.bind(1, photo_id);
    st.step();
    return photo_json(st.column_int(0), photo_id, st.column_int(1), st.column_text(2),
                      static_cast<int>(st.column_int(3)), static_cast<int>(st.column_int(4)));
}

void PhotoService::remove_photo_files(std::int64_t house_id, std::int64_t photo_id) const {
    const fs::path dir = house_dir(house_id);
    const std::string prefix = std::to_string(photo_id) + "_";
    for (const char* name : {"orig.jpg", "orig.png", "opt.jpg", "min.jpg"}) {
        std::error_code e;
        fs::remove(dir / (prefix + name), e);
        if (e) log().warn("No se pudo borrar " + to_utf8(dir / (prefix + name)) + ": " + e.message());
    }
    std::error_code ec;
    if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) fs::remove(dir, ec);
}

void PhotoService::remove(std::int64_t photo_id) {
    std::int64_t house_id = 0;
    {
        Transaction tx(db_);
        {
            Statement st = db_.prepare("SELECT casa_id FROM foto WHERE id = ?");
            st.bind(1, photo_id);
            if (!st.step()) throw NotFoundError("La foto no existe");
            house_id = st.column_int(0);
        }
        {
            Statement del = db_.prepare("DELETE FROM foto WHERE id = ?");
            del.bind(1, photo_id);
            del.step();
        }
        tx.commit();
    }
    remove_photo_files(house_id, photo_id);
}

void PhotoService::remove_house_folder(std::int64_t house_id) {
    std::error_code ec;
    fs::remove_all(house_dir(house_id), ec);
    if (ec) {
        log().warn("No se pudo borrar la carpeta de fotos de la casa " + std::to_string(house_id) + ": " +
                   ec.message());
    }
}

std::map<std::int64_t, std::string> PhotoService::cover_thumbnails() const {
    std::map<std::int64_t, std::string> out;
    Statement st = db_.prepare(
        "SELECT f.casa_id, f.id FROM foto f WHERE f.id = "
        "(SELECT id FROM foto WHERE casa_id = f.casa_id ORDER BY orden, id LIMIT 1)");
    while (st.step()) out[st.column_int(0)] = url_for(st.column_int(0), st.column_int(1), "min");
    return out;
}

void PhotoService::cleanup_tmp(int max_age_hours) {
    std::error_code ec;
    if (!fs::is_directory(paths_.tmp_dir, ec)) return;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(max_age_hours);
    for (const auto& e : fs::directory_iterator(paths_.tmp_dir, ec)) {
        const std::string name = to_utf8(e.path().filename());
        if (!e.is_directory() || name.rfind("foto-", 0) != 0) continue;
        std::error_code e2;
        if (e.last_write_time(e2) < limit) fs::remove_all(e.path(), e2);
    }
}

std::vector<std::string> PhotoService::log_orphan_folders() const {
    std::vector<std::string> orphans;
    std::error_code ec;
    if (!fs::is_directory(paths_.photos_dir, ec)) return orphans;
    for (const auto& e : fs::directory_iterator(paths_.photos_dir, ec)) {
        if (!e.is_directory()) continue;
        const std::string name = to_utf8(e.path().filename());
        const bool numeric = !name.empty() && name.size() < 19 &&
                             std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); });
        if (numeric && house_exists(std::stoll(name))) continue;
        orphans.push_back(name);
        log().warn("Carpeta de fotos sin casa (no se borra): " + to_utf8(e.path()));
    }
    return orphans;
}

fs::path PhotoService::media_file(const std::string& house_dir_name, const std::string& file) const {
    static const std::regex dir_re("^[0-9]{1,18}$");
    static const std::regex file_re("^[0-9]{1,18}_(opt|min)\\.jpg$");
    if (!std::regex_match(house_dir_name, dir_re) || !std::regex_match(file, file_re)) return {};
    const fs::path p = paths_.photos_dir / house_dir_name / file;
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : fs::path{};
}

}  // namespace wcatalog
