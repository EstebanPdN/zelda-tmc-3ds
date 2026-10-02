#pragma once
/*
 * port_glslp_parser.h — Stage 5+C scaffold for the libretro .glslp
 * preset-format parser.
 *
 * **Not implemented yet.** This header documents the data structures
 * and parser API a future session would build. Together with the
 * multi-pass infrastructure shipped in Stage 5+A (intermediate
 * texture, prepass pipeline), it would let TMC drop the
 * libretro/glsl-shaders repo into assets/shaders/ and Just Work.
 *
 * Why this file exists in this state: a full .glslp runtime is a
 * 2–4 week project (parser + libretro GLSL preprocessor + render-
 * graph executor + LUT loader + standard-uniform wiring). Rather
 * than half-implement it, this scaffold names every piece so the
 * next implementation session starts with a known design instead
 * of staring at the libretro spec from scratch.
 *
 * ----------------------------------------------------------------
 * libretro .glslp format (INI-like):
 *
 *   shaders = N                              # pass count
 *   feedback_pass = K                        # optional history pass
 *
 *   shader0 = "shaders/foo/bar.glsl"
 *   filter_linear0 = false                   # linear vs nearest sampling
 *   wrap_mode0 = clamp_to_border             # clamp_to_border|clamp_to_edge|repeat|mirrored_repeat
 *   mipmap_input0 = false
 *   alias0 = "PassName"                      # optional name for later passes to sample
 *   float_framebuffer0 = false               # rgba16f intermediate vs rgba8
 *   srgb_framebuffer0 = false
 *   scale_type_x0 = source                   # source|viewport|absolute
 *   scale_type_y0 = source
 *   scale_x0 = 1.0
 *   scale_y0 = 1.0
 *   # (or `scale_type0`/`scale0` to set both axes at once)
 *
 *   shader1 = ...
 *
 *   textures = "LUT1;LUT2"                   # external lookup textures
 *   LUT1 = "shaders/foo/lut1.png"
 *   LUT1_linear = true
 *   LUT1_wrap_mode = clamp_to_border
 *   LUT1_mipmap = false
 *
 *   parameters = "param0;param1"             # runtime knobs (#pragma parameter in shader)
 *   param0 = 0.5
 *
 * ----------------------------------------------------------------
 * Implementation plan, in dependency order:
 *
 *   1. INI tokenizer (~80 LOC) — parse key = value pairs, skip
 *      comments (# and ;), handle quoted string values, expand
 *      relative paths against the .glslp file's directory.
 *
 *   2. .glslp loader (~120 LOC) — read all keys into a GlslpPreset
 *      struct (defined below). Validate pass count and per-pass
 *      indices. Resolve relative shader / LUT paths to absolute.
 *
 *   3. Libretro GLSL preprocessor (~150 LOC) — substitute the
 *      libretro-specific macros into modern GLSL 450:
 *        COMPAT_PRECISION  → empty
 *        COMPAT_VARYING    → `in` (FS) / `out` (VS)
 *        COMPAT_ATTRIBUTE  → `layout(location=N) in`
 *        COMPAT_TEXTURE    → `texture`
 *        gl_FragColor      → `oColor` (declare layout(location=0))
 *        gl_TexCoord[0]    → `vTexCoord`
 *        Extract `#pragma parameter name "label" default min max step`
 *        and produce uniform declarations for them.
 *        Detect `#pragma stage vertex` / `#pragma stage fragment`
 *        and emit the matching half of the file.
 *
 *   4. GLSL → SPIR-V at runtime — link against glslang at build
 *      time, or invoke `glslangValidator` from the binary (uglier
 *      but simpler). Cache compiled .spv blobs in assets/shaders/.cache/
 *      keyed on (sourcehash, glslang_version) so repeated launches
 *      skip the compile.
 *
 *   5. Multi-pass render-graph executor (~250 LOC) — extends the
 *      existing two-pass infrastructure (Stage 5+A) to N passes:
 *      - allocate one intermediate SDL_GPUTexture per pass at the
 *        computed size (scale_type × scale × source/viewport)
 *      - wire pass[i].output → pass[i+1].input
 *      - bind any named-alias passes that pass[i] references via
 *        the libretro `PassPrev1`/`Pass1`/`<Alias>` uniform names
 *      - upload LUT textures once at preset-load time and bind to
 *        named samplers per pass
 *      - push the standard libretro uniforms each frame:
 *          MVPMatrix, OutputSize, TextureSize, InputSize,
 *          FrameCount, FrameDirection, OriginalSize, FinalViewportSize
 *      - push the user-tunable #pragma parameters from the preset
 *        defaults (a Stage 5+D could expose these in the F8 menu).
 *
 *   6. PNG/TGA LUT loader (~50 LOC) — for the `textures = ...`
 *      entries. libpng is already linked; TGA is straightforward
 *      to hand-decode for the small headers libretro shaders use.
 *
 *   7. F8 picker integration — add a "Shader preset" submenu that
 *      lists every `.glslp` file under assets/shaders/. Switching
 *      a preset tears down the active GlslpRuntime and loads a new
 *      one; mid-frame swaps are not expected.
 *
 * Quirks the implementation will need to handle:
 *   - Some shaders use deprecated `gl_FragCoord` semantics (origin
 *     bottom-left in GL vs top-left in Vulkan). Add a `#define`d
 *     `FragCoord = vec2(gl_FragCoord.x, OutputSize.y - gl_FragCoord.y)`
 *     in the preprocessor when the source declares
 *     `#pragma format ARGB16FX_FLIP_Y`.
 *   - Several presets use `#include` to share helper headers.
 *     Implement a minimal include resolver, relative to the
 *     including file's dir.
 *   - Feedback passes (`feedback_pass = N`) need two intermediate
 *     textures per such pass — one for "current frame output",
 *     one for "previous frame output (PassFeedback)". Ping-pong.
 *
 * ----------------------------------------------------------------
 * Estimated effort (focused work, not counting interruptions):
 *   1+2  ~1 day      — parser + loader
 *   3    ~1 day      — preprocessor
 *   4    ~0.5 day    — glslang link + cache
 *   5    ~1 week     — render-graph executor (the hard part)
 *   6    ~0.5 day    — LUT loader
 *   7    ~0.5 day    — F8 picker
 *
 * Total: ~2 weeks for a working drop-in runtime that handles the
 * most-used libretro presets (CRT-Geom, Lottes, NTSC, etc.). Add
 * another week to cover edge cases (feedback passes, mipmap inputs,
 * float framebuffers, all wrap modes).
 *
 * ----------------------------------------------------------------
 * Data structures the implementation will populate:
 */

#ifdef __cplusplus
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace PortGlslp {

enum class ScaleType { Source, Viewport, Absolute };
enum class WrapMode  { ClampBorder, ClampEdge, Repeat, MirroredRepeat };

struct ShaderPass {
    std::string path;                    /* resolved abs path to .glsl */
    bool        filter_linear   = false;
    WrapMode    wrap            = WrapMode::ClampBorder;
    bool        mipmap_input    = false;
    bool        srgb_framebuffer = false;
    bool        float_framebuffer = false;
    ScaleType   scale_type_x    = ScaleType::Source;
    ScaleType   scale_type_y    = ScaleType::Source;
    float       scale_x         = 1.0f;
    float       scale_y         = 1.0f;
    std::optional<std::string> alias;     /* for `aliasN = "Foo"` references */
};

struct LutTexture {
    std::string name;                    /* e.g. "LUT1" — the uniform name shaders use */
    std::string path;                    /* resolved abs path to .png/.tga */
    bool        linear  = true;
    WrapMode    wrap    = WrapMode::ClampBorder;
    bool        mipmap  = false;
};

struct ShaderParam {
    std::string id;                       /* matches #pragma parameter ID */
    std::string label;                    /* user-visible (for F8 menu) */
    float       default_value = 0.0f;
    float       min_value     = 0.0f;
    float       max_value     = 1.0f;
    float       step_value    = 0.01f;
    float       current_value = 0.0f;     /* runtime mutable */
};

struct Preset {
    std::vector<ShaderPass>  passes;
    std::vector<LutTexture>  luts;
    std::vector<ShaderParam> parameters;
    std::optional<int>       feedback_pass;
    std::string              source_path; /* for relative-path resolution */
};

/* Step 2: returns the parsed preset, or nullopt on a fatal parse
 * error. Missing referenced files (shaders / LUTs) warn but don't
 * fail load. */
std::optional<Preset> LoadPresetFile(const char* glslp_path);

/* Step 3 output: a libretro .glsl source rewritten as GLSL 450 ready
 * for glslang. Stored as two separate strings — libretro presets
 * traditionally inline both stages in the same .glsl file separated
 * by `#pragma stage vertex` / `#pragma stage fragment` blocks, but
 * the SDL_GPU pipeline expects them as distinct SPIR-V modules. */
struct PreprocessedShader {
    std::string              vertex_glsl;
    std::string              fragment_glsl;
    /* Parameters discovered via `#pragma parameter` directives in
     * the source. The .glslp file's `parameters = ...` line lists
     * which subset to expose at runtime, and supplies optional
     * default-value overrides; this list provides the label / min /
     * max / step metadata that the .glslp file doesn't carry. */
    std::vector<ShaderParam> parameters;
    /* Names of `uniform sampler2D` declarations in the source other
     * than the built-in `Texture` (primary pass input at set=2,
     * binding=0). Order matches the order they appear in source;
     * the preprocessor assigns layout(set=2, binding=1+i) to each.
     * Runtime walks this list, finds the matching LUT in
     * preset.luts by name, and binds at sampler slot 1+i. */
    std::vector<std::string> lut_sampler_names;
};

/* Step 3: rewrite libretro-flavoured GLSL into GLSL 450 stage modules.
 * `source_text` is the file body (already read by the caller).
 * Returns nullopt only when the source contains constructs we can't
 * mechanically rewrite — most real libretro shaders are accepted. */
std::optional<PreprocessedShader> PreprocessLibretroGlsl(const std::string& source_text);

/* Step 4: invoke glslangValidator at runtime to compile a GLSL 450
 * source string to a SPIR-V bytecode buffer. `stage` selects vertex
 * vs fragment. On success returns the .spv bytes; on failure logs
 * glslang's diagnostic to stderr and returns nullopt.
 *
 * Caching: a content hash of `glsl_source` keys the result in
 * <cache_dir>/<hash>.<stage>.spv. Subsequent calls with the same
 * source skip the compile. Set `cache_dir` empty to disable caching
 * (forces a fresh compile every call — useful for shader devs). */
enum class ShaderStage { Vertex, Fragment };

std::optional<std::vector<uint8_t>>
CompileGlslToSpirv(const std::string& glsl_source,
                   ShaderStage        stage,
                   const std::string& cache_dir);

/* Step 6: decoded LUT image data. ABGR8888 little-endian (byte 0 = R,
 * etc) to match SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM. Origin is
 * top-left (we flip TGA's bottom-left origin during decode). */
struct DecodedImage {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;  /* width * height * 4 bytes */
};

/* Load a LUT image from disk. Supports PNG (via libpng) and the
 * common subset of TGA (24/32-bit uncompressed, bottom-left origin
 * auto-flipped). Returns nullopt on read/decode error with a stderr
 * diagnostic. */
std::optional<DecodedImage> LoadLutImage(const std::string& path);

}  // namespace PortGlslp

#endif  /* __cplusplus */

/* C-callable API the runtime will use once the parser exists. None
 * are implemented yet. */
#ifdef __cplusplus
extern "C" {
#endif

/* Load a .glslp preset and stand up its multi-pass GPU pipeline.
 * Returns 1 on success, 0 on error. On success the next
 * Port_GPU_PresentFrame calls render through the new preset.
 * Replaces any currently-active preset. */
int  Port_GlslpRuntime_Load(const char* glslp_path);

/* Tear down the active preset (if any). Falls back to the
 * stock PortGpuFilter pipeline-switcher (Stages 3–5 lcd_grid /
 * scanline / handheld / vignette / crt_*). */
void Port_GlslpRuntime_Unload(void);

/* Whether a .glslp preset is currently driving presentation. */
int  Port_GlslpRuntime_IsActive(void);

#ifdef __cplusplus
}
#endif
