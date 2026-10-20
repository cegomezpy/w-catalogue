#include "runtime/shell_open.hpp"

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cwctype>

namespace fs = std::filesystem;

namespace wcatalog {

bool open_path_in_shell(const fs::path& path) {
    std::error_code ec;
    const bool dir = fs::is_directory(path, ec);
    HINSTANCE rc = ShellExecuteW(nullptr, dir ? L"explore" : L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

bool is_path_within(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    const fs::path b = fs::canonical(base, ec);
    if (ec) return false;
    const fs::path p = fs::canonical(path, ec);
    if (ec) return false;

    auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };
    auto bi = b.begin();
    auto pi = p.begin();
    for (; bi != b.end(); ++bi, ++pi) {
        if (pi == p.end() || lower(bi->wstring()) != lower(pi->wstring())) return false;
    }
    return true;
}

}  // namespace wcatalog
