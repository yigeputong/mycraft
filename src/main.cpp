#include "client/Window.h"

using namespace mycraft;

int main(int argc, char* argv[]) {
    Window& window = Window::getInstance();
    if (window.Init("Mycraft v0.0.0", 800, 600, Window::RenderAPItype::OPENGL)) {
        window.Run();
    }
    window.Shutdown();

    return 0;
}