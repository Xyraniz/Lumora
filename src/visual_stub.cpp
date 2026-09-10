#include "lumora.h"

#include <iostream>

int runVisual(const char* /*path*/, int /*argc*/, char** /*argv*/, bool /*sandbox*/)
{
    std::cerr << "visual mode is unavailable: Lumora was built without SDL2, SDL2_ttf, and SDL2_image\n";
    return 2;
}
