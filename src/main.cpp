
#include <iostream>
#include <csignal>
#include <memory>
#include <atomic>
#include <chrono>
#include "spdlog/spdlog.h"

#include "diarkis/events.h"
#include "diarkis/fs_watcher.h"
#include "diarkis/state_machine.h"

std::atomic<bool> g_running = true;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        spdlog::info("Recevied close signal, shutting down...");
        g_running = false;
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    auto watcher = std::make_unique<fs::Watcher>("../test_dir", [](const events::Event& event) {
        std::string evt_type = "";
        switch (event.type) {
            case events::EventType::CREATED:
                evt_type = "CREATED";
                break;
            case events::EventType::DELETED:
                evt_type = "DELETED";
                break;
            case events::EventType::MODIFIED:
                evt_type = "MODIFIED";
                break;
            case events::EventType::MOVED:
                evt_type = "MOVED";
                break;
        }

        spdlog::info("[EVENT] {}: File = {}", evt_type, event.relative_path);
    });

    if (!watcher->start()) {
        spdlog::critical("Failed to start file system watcher");
        return -1;
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    watcher->stop();
    return 0;
}
