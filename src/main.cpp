#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include "app_context.hpp"
#include "db/migrations.hpp"
#include "fonts/font_library.hpp"
#include "http/routes_core.hpp"
#include "houses/feature_service.hpp"
#include "houses/house_service.hpp"
#include "photos/photo_service.hpp"
#include "http/routes_features.hpp"
#include "http/routes_fonts.hpp"
#include "http/routes_houses.hpp"
#include "http/routes_photos.hpp"
#include "http/routes_settings.hpp"
#include "http/routes_theme.hpp"
#include "settings/settings_service.hpp"
#include "http/routes_session.hpp"
#include "http/server.hpp"
#include "runtime/app_info.hpp"
#include "runtime/edge_launcher.hpp"
#include "runtime/fatal.hpp"
#include "runtime/instance.hpp"
#include "runtime/lifecycle.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

using namespace wcatalog;
using namespace std::chrono_literals;

namespace {

std::string local_url(int port) { return "http://127.0.0.1:" + std::to_string(port) + "/"; }

// Segundo arranque: abre una ventana hacia la instancia que ya está en marcha.
int attach_to_running_instance(const AppPaths& paths) {
    auto port = SingleInstance::read_port(paths.data_dir, 5s);
    if (!port) {
        show_fatal_error("WCatalog ya está en ejecución, pero no responde. Ciérrela desde el Administrador de tareas e intente de nuevo.");
        return 1;
    }
    WindowLaunch w = open_window(local_url(*port));
    if (w.process) CloseHandle(w.process);
    return w.opened ? 0 : 1;
}

int run() {
    AppPaths paths = make_paths(current_exe_dir());
    ensure_data_dirs(paths);
    log().open(paths.logs_dir / "app.log");

    SingleInstance instance;
    if (!instance.acquire(paths.data_dir)) {
        log().info("Segundo arranque: se abre una ventana hacia la instancia existente");
        return attach_to_running_instance(paths);
    }
    log().info(std::string("Arranque de WCatalog ") + app_version());

    std::unique_ptr<Database> db = open_app_database(paths.data_dir / "catalog.db", paths.backups_dir);

    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done = false;
    auto request_exit = [&] {
        {
            std::lock_guard<std::mutex> lock(done_mutex);
            done = true;
        }
        done_cv.notify_all();
    };

    // Gracia de 10 s tras perder la última conexión; 30 s para que la primera ventana cargue.
    Lifecycle lifecycle(10s, 30s, request_exit);

    HttpServer server(resolve_web_dir(paths.exe_dir));
    AppContext ctx;
    ctx.version = app_version();
    ctx.data_dir = paths.data_dir;
    ctx.db = db.get();
    register_core_routes(server.raw(), ctx);

    SettingsService settings(*db);
    FontLibrary fonts(*db, settings, FontPaths{resolve_included_fonts_dir(paths.exe_dir), paths.fonts_dir, paths.tmp_dir});
    fonts.load_manifest();
    fonts.cleanup_tmp();
    settings.set_font_validator([&fonts](const std::string& f) { return fonts.family_exists(f); });
    register_settings_routes(server.raw(), settings);
    register_font_routes(server.raw(), fonts);
    register_theme_routes(server.raw(), settings, fonts);
    register_system_routes(server.raw(), open_in_default_browser);

    FeatureService features(*db);
    HouseService houses(*db, settings, features);
    PhotoService photos(*db, PhotoPaths{paths.photos_dir, paths.tmp_dir});
    photos.cleanup_tmp();
    photos.log_orphan_folders();
    houses.set_photo_service(&photos);
    register_feature_routes(server.raw(), features);
    register_house_routes(server.raw(), houses);
    register_photo_routes(server.raw(), photos);
    register_session_routes(server.raw(), lifecycle);

    int port = server.start();
    instance.publish_port(port);
    lifecycle.start();

    WindowLaunch window = open_window(local_url(port));
    if (!window.opened) {
        show_fatal_error("No se pudo abrir la ventana de la aplicación. Abra manualmente " + local_url(port));
    }

    // Si el proceso de la ventana termina, se aplica la misma gracia (Edge puede delegar
    // en otro proceso, así que esto solo acelera el cierre; no es la señal principal).
    std::atomic<bool> quitting{false};
    std::thread watcher;
    if (window.process) {
        watcher = std::thread([&] {
            while (!quitting) {
                if (WaitForSingleObject(window.process, 500) == WAIT_OBJECT_0) {
                    lifecycle.window_process_exited();
                    break;
                }
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(done_mutex);
        done_cv.wait(lock, [&] { return done; });
    }

    quitting = true;
    if (watcher.joinable()) watcher.join();
    if (window.process) CloseHandle(window.process);
    lifecycle.stop();
    server.stop();
    db->close();
    log().info("Cierre ordenado");
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        return run();
    } catch (const std::exception& e) {
        show_fatal_error(e.what());
        return 1;
    }
}
