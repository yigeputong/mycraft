#pragma once

#include <SDL3/SDL.h>

namespace mycraft {

class IRenderAPI {
public:
    ~IRenderAPI() = default;
    virtual bool Init(SDL_Window* window, int width, int height) = 0;
    virtual bool HandleEvents(SDL_Event& event) = 0;
    virtual void render() = 0;
};

template<typename API>
class APITraits {
protected:

};

}