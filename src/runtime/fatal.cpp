#include "runtime/fatal.hpp"

#include <windows.h>

#include "runtime/log.hpp"

namespace wcatalog {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}  // namespace

void show_fatal_error(const std::string& message) {
    log().error("Error fatal: " + message);
    MessageBoxW(nullptr, widen(message).c_str(), L"WCatalog - Error", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

}  // namespace wcatalog
