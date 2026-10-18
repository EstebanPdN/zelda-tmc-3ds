#pragma once

#include "port_runtime_config.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Port_TouchControls_HandleEvent(const SDL_Event* event);
void Port_TouchControls_NotifyRenderSize(int width, int height);
void Port_TouchControls_NotifyGamepadUsed(void);
void Port_TouchControls_SetGamepadAvailable(bool available);
bool Port_TouchControls_InputPressed(PortInput input);
void Port_TouchControls_Render(SDL_Renderer* renderer, int windowWidth, int windowHeight);

bool Port_TouchControls_ConsumeSettingsRequest(void);

#ifdef __cplusplus
}
#endif
