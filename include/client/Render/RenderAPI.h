#pragma once

namespace mycraft {

class IRenderAPI {
public:
    ~IRenderAPI() = default;
    virtual bool Init(SDL_Window* window, int width, int height) = 0;
};

template<typename API>
class APITraits {
protected:

};

}