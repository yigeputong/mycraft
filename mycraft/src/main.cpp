#include "Engine/Engine.h"
#include "game/Game.h"
#include <cstdio>
#include <memory>

int main() {
    try {
        Eng::EngineConfig engConfig = {
            .mode          = Eng::Mode::SinglePlayer,
            .name          = "Mycraft",
            .enableNetwork = true
        };

        Eng::Engine& eng = Eng::Engine::GetInstance();

        //先用 unique_ptr 接管，Init 抛异常时自动释放，不会泄漏
        auto game = std::make_unique<game::MyGame>();

        if (!eng.Init(engConfig, game.release())) {
            std::fprintf(stderr, "[Engine] Failed to initialize!\n");
            std::fprintf(stderr, "[Engine] Error: %s\n", eng.GetLastError().c_str());
            return 1;
        }

        // ★ RAII 守卫：Run() 抛异常也能保证 Quit() 被调用
        struct EngineGuard {
            Eng::Engine& e;
            ~EngineGuard() { e.Quit(); }
        };
        EngineGuard guard{ eng };

        eng.Run();
        return 0;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "[main] Unhandled: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fputs("[main] Unhandled unknown\n", stderr);
        return 1;
    }
}