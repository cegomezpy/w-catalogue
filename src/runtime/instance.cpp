#include "runtime/instance.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

// FNV-1a: estable entre ejecuciones (std::hash no lo garantiza)
std::uint64_t fnv1a(const std::wstring& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::wstring normalized(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(p, ec);
    std::wstring w = (ec ? p : abs).wstring();
    std::transform(w.begin(), w.end(), w.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return w;
}

}  // namespace

std::string SingleInstance::mutex_name(const fs::path& data_dir) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(fnv1a(normalized(data_dir))));
    return std::string("Local\\WCatalog-") + buf;
}

SingleInstance::~SingleInstance() {
    if (!instance_file_.empty()) {
        std::error_code ec;
        fs::remove(instance_file_, ec);
    }
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
}

bool SingleInstance::acquire(const fs::path& data_dir) {
    mutex_ = CreateMutexA(nullptr, FALSE, mutex_name(data_dir).c_str());
    if (!mutex_) return true;  // sin mutex no se puede garantizar; se prefiere arrancar
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }
    instance_file_ = data_dir / ".instance";
    return true;
}

void SingleInstance::publish_port(int port) {
    if (instance_file_.empty()) return;
    std::ofstream out(instance_file_, std::ios::binary | std::ios::trunc);
    out << port;
}

std::optional<int> SingleInstance::read_port(const fs::path& data_dir, std::chrono::milliseconds wait) {
    const auto deadline = std::chrono::steady_clock::now() + wait;
    for (;;) {
        std::ifstream in(data_dir / ".instance", std::ios::binary);
        int port = 0;
        if (in >> port && port > 0 && port < 65536) return port;
        if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace wcatalog
