#include "Engine.h"
#include <iostream>
#include "game/Game.h"

int main() {
    try {
        Eng::EngineConfig engConfig = {
            .mode = Eng::Mode::SinglePlayer,
            .name = "Mycraft",
            .enableNetwork = true
        };

        Eng::Engine& eng = Eng::Engine::GetInstance();

        if (!eng.Init(engConfig, new game::MyGame)) {
            std::cerr << "[Engine] Failed to initialize!" << "\n";
            std::cerr << "[Engine] Error: " << eng.GetLastError() << "\n";
            return 1;
        }

        eng.Run();

        eng.Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[main] Unhandled: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fputs("[main] Unhandled unknown\n", stderr);
        return 1;
    }
}