#include "runtime/log.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

std::string timestamp() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    std::time_t t = clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

}  // namespace

void Logger::open(const fs::path& file, std::uintmax_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = file;
    max_bytes_ = max_bytes;
    if (out_.is_open()) out_.close();
    std::error_code ec;
    if (fs::exists(file_, ec) && fs::file_size(file_, ec) > max_bytes_) rotate_locked();
    out_.open(file_, std::ios::binary | std::ios::app);
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out_.is_open()) out_.close();
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }

void Logger::write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!out_.is_open()) return;
    if (max_bytes_ > 0 && static_cast<std::uintmax_t>(out_.tellp()) > max_bytes_) rotate_locked();
    out_ << timestamp() << " [" << level << "] " << msg << '\n';
    out_.flush();
}

void Logger::rotate_locked() {
    if (out_.is_open()) out_.close();
    fs::path old = file_;
    old += ".1";
    std::error_code ec;
    fs::remove(old, ec);
    fs::rename(file_, old, ec);
    out_.open(file_, std::ios::binary | std::ios::trunc);
}

Logger& log() {
    static Logger instance;
    return instance;
}

}  // namespace wcatalog
