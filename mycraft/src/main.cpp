#include "Engine.h"
#include <iostream>
#include "Game.h"

int main() {
    Eng::EngineConfig engConfig = {
        .mode = Eng::Mode::ClientAndServer,
        .name = "Mycraft",
    };

    Eng::Engine& eng = Eng::Engine::GetInstance();

    if (!eng.Init(engConfig, new MyGame)) {
        std::cerr << "[Engine] Failed to initialize!" << std::endl;
        std::cerr << "[Engine] Error: " << eng.GetLastError() << std::endl;
        return -1;
    }

    eng.Run();

    eng.Quit();
    return 0;
}