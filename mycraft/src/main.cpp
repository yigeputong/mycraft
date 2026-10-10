#include "Engine.h"
#include <iostream>
#include "game/Game.h"

int main() {
    Eng::EngineConfig engConfig = {
        .mode = Eng::Mode::SinglePlayer,
        .name = "Mycraft",
        .enableNetwork = true
    };

    Eng::Engine& eng = Eng::Engine::GetInstance();

    if (!eng.Init(engConfig, new game::MyGame)) {
        std::cerr << "[Engine] Failed to initialize!" << "\n";
        std::cerr << "[Engine] Error: " << eng.GetLastError() << "\n";
        return -1;
    }

    eng.Run();

    eng.Quit();
    return 0;
}