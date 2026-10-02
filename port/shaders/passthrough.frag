#version 450
// Passthrough fragment shader for the SDL_GPU presentation path.
//
// Stage 1: sample the input texture and emit unchanged. This is the
// "prove the pipeline compiles" shader. Later stages will replace this
// with the actual filter chain (CRT, scanline, lcd-grid, etc.).
//
// Binding layout matches SDL_GPU's documented convention for fragment
// samplers: set 2, binding 0 is the first combined image+sampler.

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    oColor = texture(uSource, vTexCoord);
}
