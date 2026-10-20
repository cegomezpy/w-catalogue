#include "runtime/edge_launcher.hpp"

#include <shellapi.h>

#include <cstdlib>
#include <vector>

#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::optional<fs::path> edge_from_registry(HKEY root) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::nullopt;

    std::wstring value(buf);
    if (type == REG_EXPAND_SZ) {
        wchar_t expanded[MAX_PATH * 2] = {};
        if (ExpandEnvironmentStringsW(value.c_str(), expanded, MAX_PATH * 2) > 0) value = expanded;
    }
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
    fs::path p(value);
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return p;
    return std::nullopt;
}

std::wstring env_wide(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return {};
    return std::wstring(buf, n);
}

}  // namespace

std::optional<fs::path> find_edge() {
    std::error_code ec;

    std::wstring forced = env_wide(L"WCATALOG_EDGE_PATH");
    if (!forced.empty()) {
        fs::path p(forced);
        if (fs::is_regular_file(p, ec)) return p;
        return std::nullopt;
    }

    if (auto p = edge_from_registry(HKEY_LOCAL_MACHINE)) return p;
    if (auto p = edge_from_registry(HKEY_CURRENT_USER)) return p;

    std::vector<fs::path> known;
    for (const wchar_t* var : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        std::wstring base = env_wide(var);
        if (!base.empty()) known.push_back(fs::path(base) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe");
    }
    for (const auto& p : known) {
        if (fs::is_regular_file(p, ec)) return p;
    }
    return std::nullopt;
}

fs::path edge_profile_dir() {
    std::wstring local = env_wide(L"LOCALAPPDATA");
    if (local.empty()) local = (fs::temp_directory_path()).wstring();
    return fs::path(local) / L"WCatalog" / L"edge-profile";
}

std::wstring edge_command_line(const fs::path& edge, const std::string& url, const fs::path& profile_dir) {
    std::wstring cmd = L"\"" + edge.wstring() + L"\"";
    cmd += L" --app=" + widen(url);
    cmd += L" \"--user-data-dir=" + profile_dir.wstring() + L"\"";
    cmd += L" --no-first-run --no-default-browser-check";
    return cmd;
}

bool open_in_default_browser(const std::string& url) {
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

WindowLaunch open_window(const std::string& url) {
    WindowLaunch result;

    if (auto edge = find_edge()) {
        fs::path profile = edge_profile_dir();
        std::error_code ec;
        fs::create_directories(profile, ec);

        std::wstring cmd = edge_command_line(*edge, url, profile);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            result.opened = true;
            result.used_edge = true;
            result.process = pi.hProcess;
            log().info("Ventana abierta con Edge: " + to_utf8(*edge));
            return result;
        }
        log().warn("No se pudo lanzar Edge (error " + std::to_string(GetLastError()) + "); se usa el navegador predeterminado");
    } else {
        log().warn("Edge no encontrado; se usa el navegador predeterminado");
    }

    result.opened = open_in_default_browser(url);
    if (!result.opened) log().error("No se pudo abrir el navegador predeterminado");
    return result;
}

}  // namespace wcatalog
