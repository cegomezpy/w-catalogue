#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wcatalog {

// Variable de entorno de depuración: fuerza la ruta de msedge.exe. Si apunta a un
// archivo que no existe, se trata como "Edge no encontrado" (útil para probar el respaldo).
inline constexpr const char* kEdgePathEnv = "WCATALOG_EDGE_PATH";

// Busca msedge.exe: variable de depuración, registro (App Paths) y rutas conocidas.
std::optional<std::filesystem::path> find_edge();

// Carpeta del perfil propio de la ventana: %LOCALAPPDATA%\WCatalog\edge-profile
std::filesystem::path edge_profile_dir();

// Línea de comandos con la que se lanza Edge en modo aplicación.
std::wstring edge_command_line(const std::filesystem::path& edge, const std::string& url,
                               const std::filesystem::path& profile_dir);

struct WindowLaunch {
    bool opened = false;
    bool used_edge = false;
    HANDLE process = nullptr;  // proceso de Edge si se lanzó; el llamador lo cierra con CloseHandle
};

// Abre una URL en el navegador predeterminado (ShellExecuteW).
bool open_in_default_browser(const std::string& url);

// Abre la interfaz: Edge en modo --app, o el navegador predeterminado si Edge no se encuentra.
WindowLaunch open_window(const std::string& url);

}  // namespace wcatalog
