
#include <iostream>
#include <csignal>
#include <memory>
#include <atomic>
#include <chrono>
#include "spdlog/spdlog.h"

#include "diarkis/fs.h"

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

    auto watcher = std::make_unique<fs::Watcher>("../test_dir", [](const fs::Event& event) {
        std::string evt_type = "";
        switch (event.type) {
            case fs::EventType::CREATED:
                evt_type = "CREATED";
                break;
            case fs::EventType::DELETED:
                evt_type = "DELETED";
                break;
            case fs::EventType::MODIFIED:
                evt_type = "MODIFIED";
                break;
            case fs::EventType::MOVED:
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
