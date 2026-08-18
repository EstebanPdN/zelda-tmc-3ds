#pragma once

#include "port_ppu_gpu_3ds_model.h"

#include <citro3d.h>

bool PortPpuGpu3DS_Init(void);
void PortPpuGpu3DS_Shutdown(void);
bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame);
bool PortPpuGpu3DS_DrawPrepared(void);
C3D_Tex* PortPpuGpu3DS_OutputTexture(void);
void PortPpuGpu3DS_Disable(void);
