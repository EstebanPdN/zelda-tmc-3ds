#pragma once

#include <stdbool.h>

struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

bool Port_RunBootstrapLauncher(struct SDL_Window* window);

#ifdef __cplusplus
}
#endif
