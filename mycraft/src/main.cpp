#include "Engine/Engine.h"
#include "game/Game.h"
#include <cstdio>
#include <memory>

static int MycraftMain([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
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

// 根据平台决定入口函数
#if defined(__ANDROID__)
// Android 专属入口：纯 C 风格，强制可见，让 Java 层能通过 dlsym 找到
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[]) {
    return MycraftMain(argc, argv);
}
#else
// 桌面端（Windows/Linux/macOS）标准入口
int main(int argc, char* argv[]) {
    return MycraftMain(argc, argv);
}
#endif
