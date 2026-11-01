#include <SDL3_net/SDL_net.h>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "Engine/tool/Log.h"
#include "Engine/tool/Platform.h"
#include "game/GameServer.h"

int main(int argc, char* argv[]) {
    Eng::tool::SetConsoleUTF8Output();

    // ---- 解析参数 ----
    uint16_t port = 25565;
    std::string saveDir = "./saves/world";

    if (argc >= 2) {
        int portInt = 0;
        const char* begin = argv[1];
        const char* end   = argv[1] + std::strlen(argv[1]);
        auto [ptr, ec] = std::from_chars(begin, end, portInt);
        if (ec != std::errc{} || ptr != end || portInt <= 0 || portInt > 65535) {
            std::fprintf(stderr, "[Server] Invalid port: %s\n", argv[1]);
            return 1;
        }
        port = static_cast<uint16_t>(portInt);
    }
    if (argc >= 3) saveDir = argv[2];

    // ---- 日志 ----
    std::unique_ptr<Eng::tool::Log> logger = std::make_unique<Eng::tool::Log>();
    logger->SetFileOutput("server.log");
    logger->SetConsoleOutput(true);

    logInfo(logger, "[Server] Starting on port " << port);

    // ---- SDL_net ----
    if (!NET_Init()) {
        logError(logger, "[Server] NET_Init failed: " << SDL_GetError());
        return 1;
    }

    // ---- 启动服务端 ----
    game::server::GameServer server;
    if (!server.Start(port, saveDir)) {
        logError(logger, "[Server] Failed to start");
        NET_Quit();
        return 1;
    }
    logInfo(logger, "[Server] Running. Type 'stop' + Enter to quit.");

    // ---- 主循环：等 stop / quit / exit ----
    std::cout << "> " << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "stop" || line == "quit" || line == "exit") {
            break;
        } else if (line == "help" || line == "?") {
            std::cout << "commands:\n"
                      << "  help / ?     show this\n"
                      << "  stop / quit  shutdown server\n" << std::flush;
        } else if (!line.empty()) {
            std::cout << "unknown command: '" << line << "'. Type 'help'.\n" << std::flush;
        }
        std::cout << "> " << std::flush;
    }

    server.Stop();
    NET_Quit();
    logInfo(logger, "[Server] Shutdown complete");
    return 0;
}