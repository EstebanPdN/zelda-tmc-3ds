#version 450
// Fullscreen quad vertex shader for the SDL_GPU presentation path.
//
// No vertex buffer required — we synthesise a triangle covering the
// clip-space quad from gl_VertexIndex (0, 1, 2 → bottom-left, bottom-right,
// top-left, and we draw two of these as a triangle strip / triangle list
// for a full screen). This is the standard fullscreen-pass idiom that
// avoids a vertex buffer binding for trivial presents.

layout(location = 0) out vec2 vTexCoord;

void main() {
    // Four-vertex quad indexed as a triangle strip:
    //   idx 0 → (-1, -1), uv (0, 1)
    //   idx 1 → ( 1, -1), uv (1, 1)
    //   idx 2 → (-1,  1), uv (0, 0)
    //   idx 3 → ( 1,  1), uv (1, 0)
    // (Vulkan's clip Y is +down; UV (0,0) is top-left to match SDL_GPU.)
    vec2 pos = vec2((gl_VertexIndex & 1) == 0 ? -1.0 :  1.0,
                    (gl_VertexIndex & 2) == 0 ? -1.0 :  1.0);
    vTexCoord = vec2((gl_VertexIndex & 1) == 0 ?  0.0 :  1.0,
                     (gl_VertexIndex & 2) == 0 ?  1.0 :  0.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
