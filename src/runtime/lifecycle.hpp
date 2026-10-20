#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace wcatalog {

// Decide cuándo termina la aplicación. La interfaz mantiene abierta una conexión de
// presencia; cuando no queda ninguna, se espera un periodo de gracia (para tolerar
// recargas) y, si nadie se reconecta, se invoca on_shutdown una sola vez.
class Lifecycle {
public:
    using Duration = std::chrono::milliseconds;

    // grace: espera tras quedarse sin conexiones.
    // startup_grace: espera inicial para que la primera ventana se conecte.
    Lifecycle(Duration grace, Duration startup_grace, std::function<void()> on_shutdown);
    Lifecycle(const Lifecycle&) = delete;
    Lifecycle& operator=(const Lifecycle&) = delete;
    ~Lifecycle();

    void start();
    void stop();

    void connection_opened();
    void connection_closed();

    // El proceso de la ventana terminó: se aplica el mismo periodo de gracia.
    void window_process_exited();

    int connections() const;
    bool shutdown_fired() const;

private:
    void run();
    void arm_locked(Duration after);

    Duration grace_;
    Duration startup_grace_;
    std::function<void()> on_shutdown_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    int connections_ = 0;
    bool armed_ = false;
    std::chrono::steady_clock::time_point deadline_;
    bool stopping_ = false;
    bool fired_ = false;
};

}  // namespace wcatalog
