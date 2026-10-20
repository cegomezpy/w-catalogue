#include "runtime/lifecycle.hpp"

#include <utility>

#include "runtime/log.hpp"

namespace wcatalog {

Lifecycle::Lifecycle(Duration grace, Duration startup_grace, std::function<void()> on_shutdown)
    : grace_(grace), startup_grace_(startup_grace), on_shutdown_(std::move(on_shutdown)) {}

Lifecycle::~Lifecycle() { stop(); }

void Lifecycle::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) return;
    arm_locked(startup_grace_);
    thread_ = std::thread([this] { run(); });
}

void Lifecycle::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void Lifecycle::connection_opened() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++connections_;
    armed_ = false;
    cv_.notify_all();
}

void Lifecycle::connection_closed() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connections_ > 0) --connections_;
    if (connections_ == 0) arm_locked(grace_);
}

void Lifecycle::window_process_exited() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connections_ == 0) arm_locked(grace_);
}

int Lifecycle::connections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_;
}

bool Lifecycle::shutdown_fired() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fired_;
}

void Lifecycle::arm_locked(Duration after) {
    armed_ = true;
    deadline_ = std::chrono::steady_clock::now() + after;
    cv_.notify_all();
}

void Lifecycle::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_ && !fired_) {
        if (!armed_) {
            cv_.wait(lock);
            continue;
        }
        if (cv_.wait_until(lock, deadline_) == std::cv_status::timeout && armed_ && connections_ == 0 &&
            std::chrono::steady_clock::now() >= deadline_) {
            fired_ = true;
            lock.unlock();
            log().info("Sin ventanas abiertas: la aplicación se cierra");
            if (on_shutdown_) on_shutdown_();
            return;
        }
    }
}

}  // namespace wcatalog
