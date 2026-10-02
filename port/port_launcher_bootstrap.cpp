#include "port_launcher_bootstrap.h"

#include <SDL3/SDL.h>

#ifdef launcher
#include "port_ppu.h"
#include "tmc_launcher.h"
#endif

extern "C" bool Port_RunBootstrapLauncher(struct SDL_Window* window) {
#ifdef launcher
    if (window == nullptr) {
        return true;
    }
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (renderer == nullptr) {
        return true;
    }
    Port_SetBootstrapWindow(window);
    const bool continue_app = TmcLauncher_Run(window, renderer);
    Port_SetBootstrapWindow(nullptr);
    return continue_app;
#else
    (void)window;
    return true;
#endif
}
