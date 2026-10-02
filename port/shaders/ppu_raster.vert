#version 450
/*
 * GPU PPU rasterizer — fullscreen-pass vertex shader.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Emits a clip-space quad from gl_VertexIndex (drawn as a 4-vertex triangle
 * strip). No vertex buffer. The fragment shader derives the GBA pixel (x,line)
 * straight from gl_FragCoord, so no varyings are needed.
 */
void main() {
    vec2 pos = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
                    (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
