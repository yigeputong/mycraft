#include <SDL3_net/SDL_net.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "core/Log.h"
#include "game/GameServer.h"

int main(int argc, char* argv[]) {
    // 端口：命令行可选参数，默认 25565
    uint16_t port = 25565;
    if (argc >= 2) port = static_cast<uint16_t>(std::atoi(argv[1]));

    // ---- 日志 ----
    std::unique_ptr<Eng::Log> logger = std::make_unique<Eng::Log>();
    logger->SetFileOutput("server.log");
    logger->SetConsoleOutput(true);

    logInfo(logger, "[Server] Starting on port " << port);

    // ---- SDL_net ----
    if (!NET_Init()) {
        logError(logger, "[Server] NET_Init failed: " << SDL_GetError());
        return 1;
    }

    // ---- 启动服务端（内部起独立线程跑 tick）----
    game::server::GameServer server;
    if (!server.Start(port)) {
        logError(logger, "[Server] Failed to start");
        NET_Quit();
        return 1;
    }
    logInfo(logger, "[Server] Running. Type 'stop' + Enter to quit.");

    // ---- 主循环：等 "stop" / "quit" / "exit" ----
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "stop" || line == "quit" || line == "exit") break;
    }

    server.Stop();
    NET_Quit();
    logInfo(logger, "[Server] Shutdown complete");
    return 0;
}