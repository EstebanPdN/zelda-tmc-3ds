/*
 * port_glslp_runtime.cpp — Step 5 of the .glslp runtime: pull
 * everything from Steps 1–4 together into an N-pass render graph
 * usable by port_gpu_renderer's present path.
 *
 * Current state: Load-side complete. Compiles every pass's vertex +
 * fragment SPIR-V (via Step 3's preprocessor + Step 4's glslang
 * wrapper), creates SDL_GPUShader objects, computes per-pass output
 * sizes. Pipeline creation + per-frame Present + integration with
 * port_gpu_renderer's PresentFrame are the next chunks.
 */

#include "port_glslp_parser.h"
#include "port_gpu_renderer.h"

#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

/* Per-pass GPU state: compiled SPIR-V + created shader modules.
 * Pipeline + intermediate texture come in the next chunk. */
struct PassResources {
    std::vector<uint8_t> vertex_spv;
    std::vector<uint8_t> fragment_spv;
#ifdef TMC_GPU_RENDERER
    SDL_GPUShader*           vertex_shader   = nullptr;
    SDL_GPUShader*           fragment_shader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline        = nullptr;
    /* Intermediate texture this pass renders into. nullptr for the
     * last pass (renders directly to the swapchain instead). */
    SDL_GPUTexture*          out_texture     = nullptr;
    /* Previous-frame copy of out_texture, used by feedback passes.
     * Allocated at Load only if the preset references this pass's
     * alias via `<alias>Feedback` in a downstream pass; nullptr
     * otherwise. Swapped with out_texture at the end of each frame. */
    SDL_GPUTexture*          prev_texture    = nullptr;
    bool                     needs_feedback  = false;
#endif
    /* Resolved output dimensions in pixels. Computed at Load time
     * given the source framebuffer (240×160) and final viewport
     * (window size). Refreshes if the viewport changes — TODO. */
    int out_w = 0;
    int out_h = 0;
    /* Parameters declared by this pass's source via `#pragma parameter`.
     * Order matches the ShaderParams uniform-block layout the
     * preprocessor emitted (preprocessor walks the pragmas in source
     * order and emits the block fields in the same order). PresentFrame
     * pushes the values in this order; runtime override values come
     * from g_runtime.preset.parameters via ID lookup. */
    std::vector<PortGlslp::ShaderParam> params;
    /* LUT sampler names this pass declared in source order. Each
     * binding index = 1 + position in this vector (binding 0 is the
     * primary pass input). PresentFrame matches each name against
     * g_runtime.luts; missing entries fall back to a transparent
     * placeholder. */
    std::vector<std::string> lut_names;
};

#ifdef TMC_GPU_RENDERER
struct LutResource {
    std::string     name;
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUSampler* sampler = nullptr;  /* per-LUT — honours preset's linear/wrap flags */
    int             w = 0;
    int             h = 0;
};

static SDL_GPUSamplerAddressMode WrapModeToSdl(PortGlslp::WrapMode w) {
    switch (w) {
        case PortGlslp::WrapMode::ClampBorder:   return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;  /* SDL_GPU lacks a true border mode; clamp is the closest */
        case PortGlslp::WrapMode::ClampEdge:     return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case PortGlslp::WrapMode::Repeat:        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        case PortGlslp::WrapMode::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}
#endif

struct Runtime {
    bool                       loaded = false;
    PortGlslp::Preset          preset;
    std::vector<PassResources> passes;
#ifdef TMC_GPU_RENDERER
    SDL_GPUDevice*       device         = nullptr;
    SDL_GPUSampler*      sampler_linear = nullptr;
    SDL_GPUSampler*      sampler_nearest = nullptr;
    SDL_GPUTextureFormat swap_format    = SDL_GPU_TEXTUREFORMAT_INVALID;
    /* LUT textures keyed by name (PNG/TGA decoded + uploaded at Load
     * time). Empty when the preset has no `textures = ...` line. */
    std::vector<LutResource> luts;
    /* Stage-5 MVP: assume swapchain ~ 960×640 at startup and don't
     * re-resize intermediate textures when the window changes. A
     * later iteration adds an OnResize hook that re-allocates the
     * viewport-scale passes. */
    Uint32               assumed_vp_w   = 960;
    Uint32               assumed_vp_h   = 640;
    /* Frame-history ring buffer of GBA framebuffer copies. Used to
     * provide `PrevTexture` / `Prev1Texture` / ... / `Prev6Texture`
     * sampler bindings for shaders that do temporal effects
     * (motion blur, anti-flicker, interlace shutter). Lazily
     * allocated at PresentFrame when needed (src_w/src_h known
     * only then). */
    static constexpr int      kPrevCount = 7;
    SDL_GPUTexture*           prev_ring[kPrevCount] = {};
    int                       prev_head = 0;   /* slot that will hold THIS frame's copy */
    int                       prev_ring_w = 0;
    int                       prev_ring_h = 0;
#endif
};

static Runtime g_runtime;

static bool ReadEntireFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto end = f.tellg();
    f.seekg(0);
    out.resize((size_t)end);
    if (!f.read(out.data(), out.size())) {
        out.clear();
        return false;
    }
    return true;
}

/* Compute pass i's output (width, height) from its ScaleType/scale
 * factors. For the MVP we treat the source as 240×160 (the GBA fb)
 * and the viewport as the swapchain dimensions. Future stage 5
 * iteration: chain-source-of-previous-pass for scale_type=source. */
static void ResolvePassSize(const PortGlslp::ShaderPass& p,
                            int src_w, int src_h,
                            int vp_w,  int vp_h,
                            int& out_w, int& out_h) {
    using PortGlslp::ScaleType;

    auto resolve_axis = [&](ScaleType st, float sc, int src, int vp) -> int {
        switch (st) {
            case ScaleType::Source:   return (int)((float)src * sc);
            case ScaleType::Viewport: return (int)((float)vp  * sc);
            case ScaleType::Absolute: return (int)sc;
        }
        return src;
    };
    out_w = resolve_axis(p.scale_type_x, p.scale_x, src_w, vp_w);
    out_h = resolve_axis(p.scale_type_y, p.scale_y, src_h, vp_h);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;
}

}  // namespace


extern "C" int Port_GlslpRuntime_Load(const char* glslp_path) {
    /* Stage 5 MVP load:
     *   1. Tear down any previous preset state.
     *   2. Parse the .glslp file via LoadPresetFile.
     *   3. For each pass: read shader source, preprocess, compile
     *      vertex + fragment SPIR-V (cached in <preset_dir>/.spv_cache/).
     *   4. Stash SPIR-V buffers in g_runtime.passes; create
     *      SDL_GPUShader objects so the next chunk can build pipelines.
     *   5. Compute pass output sizes against fb 240×160 and an
     *      assumed viewport 960×640 (refresh when window resizes).
     */
    Port_GlslpRuntime_Unload();

    auto preset_opt = PortGlslp::LoadPresetFile(glslp_path);
    if (!preset_opt) {
        std::fprintf(stderr, "[glslp] Load: preset parse failed for %s\n", glslp_path);
        return 0;
    }
    g_runtime.preset = std::move(*preset_opt);

#ifdef TMC_GPU_RENDERER
    g_runtime.device = Port_GPU_GetDevice();
    if (!g_runtime.device) {
        std::fprintf(stderr, "[glslp] Load: SDL_GPU device unavailable; .glslp runtime needs --gpu_renderer=y\n");
        return 0;
    }
    if (!Port_GPU_SupportsGlslpRuntime()) {
        std::fprintf(stderr, "[glslp] Load: active SDL_GPU shader format does not support SPIR-V presets\n");
        return 0;
    }
    g_runtime.swap_format = Port_GPU_GetSwapchainFormat();

    /* Two shared samplers — linear and nearest. Per-pass filter_linear
     * picks which one to bind. address_mode honours the preset's
     * wrap_mode (clamp by default). */
    if (!g_runtime.sampler_linear) {
        SDL_GPUSamplerCreateInfo sci = {};
        sci.min_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        g_runtime.sampler_linear = SDL_CreateGPUSampler(g_runtime.device, &sci);
    }
    if (!g_runtime.sampler_nearest) {
        SDL_GPUSamplerCreateInfo sci = {};
        sci.min_filter     = SDL_GPU_FILTER_NEAREST;
        sci.mag_filter     = SDL_GPU_FILTER_NEAREST;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        g_runtime.sampler_nearest = SDL_CreateGPUSampler(g_runtime.device, &sci);
    }
#endif

    /* Cache directory next to the preset itself so different presets
     * keep their compiled .spv blobs isolated. */
    namespace fs = std::filesystem;
    fs::path cache_dir = fs::path(glslp_path).parent_path() / ".spv_cache";

    g_runtime.passes.resize(g_runtime.preset.passes.size());

#ifdef TMC_GPU_RENDERER
    /* Decode + upload every LUT referenced by the preset before the
     * per-pass loop runs (some passes may reference them by name).
     * Failures log but continue — missing LUTs become opaque-magenta
     * placeholders so the bind still has a valid texture and the
     * shader's `texture(LUTN, ...)` returns something rather than
     * crashing. */
    for (const auto& l : g_runtime.preset.luts) {
        LutResource lr;
        lr.name = l.name;
        auto img = PortGlslp::LoadLutImage(l.path);
        if (!img) {
            std::fprintf(stderr, "[glslp] Load: LUT '%s' decode failed; using magenta placeholder\n",
                         l.name.c_str());
            img = PortGlslp::DecodedImage{};
            img->width = img->height = 1;
            img->rgba = { 255, 0, 255, 255 };
        }
        SDL_GPUTextureCreateInfo tci = {};
        tci.type   = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width  = (Uint32)img->width;
        tci.height = (Uint32)img->height;
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        lr.texture = SDL_CreateGPUTexture(g_runtime.device, &tci);
        lr.w = img->width; lr.h = img->height;
        if (!lr.texture) {
            std::fprintf(stderr, "[glslp] Load: LUT '%s' GPU texture create failed: %s\n",
                         l.name.c_str(), SDL_GetError());
            continue;
        }

        /* Per-LUT sampler honouring the preset's linear / wrap_mode
         * settings. mipmap_mode stays NEAREST regardless of l.mipmap
         * until we actually generate mips at upload — TODO. */
        {
            SDL_GPUSamplerCreateInfo sci = {};
            sci.min_filter     = l.linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
            sci.mag_filter     = l.linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
            sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sci.address_mode_u = WrapModeToSdl(l.wrap);
            sci.address_mode_v = WrapModeToSdl(l.wrap);
            sci.address_mode_w = WrapModeToSdl(l.wrap);
            lr.sampler = SDL_CreateGPUSampler(g_runtime.device, &sci);
        }

        /* One-shot upload via a transfer buffer. Free the buffer
         * immediately after submit — LUTs are static, no reuse needed. */
        SDL_GPUTransferBufferCreateInfo tbci = {};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = (Uint32)(img->width * img->height * 4);
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_runtime.device, &tbci);
        if (tb) {
            void* mapped = SDL_MapGPUTransferBuffer(g_runtime.device, tb, /*cycle=*/false);
            if (mapped) {
                std::memcpy(mapped, img->rgba.data(), img->rgba.size());
                SDL_UnmapGPUTransferBuffer(g_runtime.device, tb);
                SDL_GPUCommandBuffer* upload_cmd = SDL_AcquireGPUCommandBuffer(g_runtime.device);
                if (upload_cmd) {
                    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(upload_cmd);
                    SDL_GPUTextureTransferInfo src = {};
                    src.transfer_buffer = tb;
                    src.offset = 0;
                    src.pixels_per_row = (Uint32)img->width;
                    src.rows_per_layer = (Uint32)img->height;
                    SDL_GPUTextureRegion dst = {};
                    dst.texture = lr.texture;
                    dst.w = (Uint32)img->width;
                    dst.h = (Uint32)img->height;
                    dst.d = 1;
                    SDL_UploadToGPUTexture(cp, &src, &dst, /*cycle=*/false);
                    SDL_EndGPUCopyPass(cp);
                    SDL_SubmitGPUCommandBuffer(upload_cmd);
                }
            }
            SDL_ReleaseGPUTransferBuffer(g_runtime.device, tb);
        }
        g_runtime.luts.push_back(lr);
    }
#endif

    const int kSrcW = 240, kSrcH = 160;
    const int kVpW = 960, kVpH = 640;  /* assumed; refresh on resize */

    for (size_t i = 0; i < g_runtime.preset.passes.size(); ++i) {
        const auto& pdef = g_runtime.preset.passes[i];
        auto&       p    = g_runtime.passes[i];

        std::string source_text;
        if (!ReadEntireFile(pdef.path, source_text)) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: failed to read %s\n",
                         i, pdef.path.c_str());
            Port_GlslpRuntime_Unload();
            return 0;
        }

        auto pp = PortGlslp::PreprocessLibretroGlsl(source_text);
        if (!pp) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: preprocessor returned nullopt\n", i);
            Port_GlslpRuntime_Unload();
            return 0;
        }
        p.params    = std::move(pp->parameters);
        p.lut_names = std::move(pp->lut_sampler_names);

        auto vspv = PortGlslp::CompileGlslToSpirv(
            pp->vertex_glsl, PortGlslp::ShaderStage::Vertex, cache_dir.string());
        auto fspv = PortGlslp::CompileGlslToSpirv(
            pp->fragment_glsl, PortGlslp::ShaderStage::Fragment, cache_dir.string());
        if (!vspv || !fspv) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: glslang failed\n", i);
            Port_GlslpRuntime_Unload();
            return 0;
        }
        p.vertex_spv   = std::move(*vspv);
        p.fragment_spv = std::move(*fspv);

#ifdef TMC_GPU_RENDERER
        SDL_GPUShaderCreateInfo vci = {};
        vci.code_size            = p.vertex_spv.size();
        vci.code                 = p.vertex_spv.data();
        vci.entrypoint           = "main";
        vci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
        vci.stage                = SDL_GPU_SHADERSTAGE_VERTEX;
        vci.num_samplers         = 0;
        vci.num_uniform_buffers  = p.params.empty() ? 1 : 2;  /* LibretroUniforms (+ ShaderParams) */
        p.vertex_shader = SDL_CreateGPUShader(g_runtime.device, &vci);

        SDL_GPUShaderCreateInfo fci = {};
        fci.code_size            = p.fragment_spv.size();
        fci.code                 = p.fragment_spv.data();
        fci.entrypoint           = "main";
        fci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
        fci.stage                = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fci.num_samplers         = 1 + (Uint32)p.lut_names.size();  /* primary input + LUTs */
        fci.num_uniform_buffers  = p.params.empty() ? 1 : 2;  /* LibretroUniforms + ShaderParams */
        p.fragment_shader = SDL_CreateGPUShader(g_runtime.device, &fci);

        if (!p.vertex_shader || !p.fragment_shader) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: SDL_CreateGPUShader failed: %s\n",
                         i, SDL_GetError());
            Port_GlslpRuntime_Unload();
            return 0;
        }
#endif

        ResolvePassSize(pdef, kSrcW, kSrcH, kVpW, kVpH, p.out_w, p.out_h);

#ifdef TMC_GPU_RENDERER
        const bool is_last = (i == g_runtime.preset.passes.size() - 1);

        /* Intermediate texture — every pass except the last writes
         * to its own offscreen texture; the last pass renders to
         * the swapchain. Use SDR R8G8B8A8 for now; pdef.float_framebuffer
         * could pick R16G16B16A16_FLOAT in a future iteration. */
        if (!is_last) {
            SDL_GPUTextureCreateInfo tci = {};
            tci.type   = SDL_GPU_TEXTURETYPE_2D;
            tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            tci.width  = (Uint32)p.out_w;
            tci.height = (Uint32)p.out_h;
            tci.layer_count_or_depth = 1;
            tci.num_levels           = 1;
            p.out_texture = SDL_CreateGPUTexture(g_runtime.device, &tci);
            if (!p.out_texture) {
                std::fprintf(stderr, "[glslp] Load: pass %zu: intermediate texture create failed: %s\n",
                             i, SDL_GetError());
                Port_GlslpRuntime_Unload();
                return 0;
            }
        }

        /* Pipeline — target format matches what this pass renders to.
         * Last pass renders to swapchain (sSwapFormat); earlier
         * passes to R8G8B8A8 intermediates. */
        SDL_GPUColorTargetDescription ct = {};
        ct.format = is_last ? g_runtime.swap_format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ct.blend_state.enable_blend = false;
        SDL_GPUGraphicsPipelineTargetInfo ti = {};
        ti.num_color_targets = 1;
        ti.color_target_descriptions = &ct;

        SDL_GPUGraphicsPipelineCreateInfo gpci = {};
        gpci.vertex_shader            = p.vertex_shader;
        gpci.fragment_shader          = p.fragment_shader;
        gpci.primitive_type           = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        gpci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
        gpci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
        gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        gpci.target_info = ti;
        p.pipeline = SDL_CreateGPUGraphicsPipeline(g_runtime.device, &gpci);
        if (!p.pipeline) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: pipeline create failed: %s\n",
                         i, SDL_GetError());
            Port_GlslpRuntime_Unload();
            return 0;
        }
#endif
    }

#ifdef TMC_GPU_RENDERER
    /* Feedback-pass scan: any pass whose lut_names contains
     * `<alias>Feedback` for an earlier-pass alias triggers prev_texture
     * allocation on that earlier pass. We swap current↔prev at frame
     * end so the next frame samples the previous frame's output. */
    int feedback_passes = 0;
    for (size_t i = 0; i < g_runtime.passes.size(); ++i) {
        for (const auto& sname : g_runtime.passes[i].lut_names) {
            const std::string kSuffix = "Feedback";
            if (sname.size() <= kSuffix.size()) continue;
            if (sname.compare(sname.size() - kSuffix.size(), kSuffix.size(),
                              kSuffix) != 0) continue;
            std::string alias = sname.substr(0, sname.size() - kSuffix.size());
            for (size_t j = 0; j < g_runtime.preset.passes.size(); ++j) {
                if (g_runtime.preset.passes[j].alias
                    && *g_runtime.preset.passes[j].alias == alias) {
                    g_runtime.passes[j].needs_feedback = true;
                    break;
                }
            }
        }
    }
    for (size_t i = 0; i < g_runtime.passes.size(); ++i) {
        auto& p = g_runtime.passes[i];
        if (!p.needs_feedback || !p.out_texture) continue;
        SDL_GPUTextureCreateInfo tci = {};
        tci.type   = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        tci.width  = (Uint32)p.out_w;
        tci.height = (Uint32)p.out_h;
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        p.prev_texture = SDL_CreateGPUTexture(g_runtime.device, &tci);
        if (p.prev_texture) ++feedback_passes;
    }
#endif

    g_runtime.loaded = true;
#ifdef TMC_GPU_RENDERER
    std::fprintf(stderr,
        "[glslp] loaded preset '%s' — %zu passes, %zu LUTs bound, %zu parameters%s%s\n",
        glslp_path,
        g_runtime.preset.passes.size(),
        g_runtime.luts.size(),
        g_runtime.preset.parameters.size(),
        feedback_passes ? ", " : "",
        feedback_passes ? (std::to_string(feedback_passes) + " feedback").c_str() : "");
#else
    std::fprintf(stderr, "[glslp] loaded preset '%s' (TMC_GPU_RENDERER off — preset parsed only, not driving present)\n",
                 glslp_path);
#endif
    return 1;
}

extern "C" void Port_GlslpRuntime_Unload(void) {
#ifdef TMC_GPU_RENDERER
    if (g_runtime.device) {
        for (auto& p : g_runtime.passes) {
            if (p.pipeline)        { SDL_ReleaseGPUGraphicsPipeline(g_runtime.device, p.pipeline);        p.pipeline = nullptr; }
            if (p.out_texture)     { SDL_ReleaseGPUTexture(g_runtime.device, p.out_texture);              p.out_texture = nullptr; }
            if (p.prev_texture)    { SDL_ReleaseGPUTexture(g_runtime.device, p.prev_texture);             p.prev_texture = nullptr; }
            p.needs_feedback = false;
            if (p.fragment_shader) { SDL_ReleaseGPUShader(g_runtime.device, p.fragment_shader);           p.fragment_shader = nullptr; }
            if (p.vertex_shader)   { SDL_ReleaseGPUShader(g_runtime.device, p.vertex_shader);             p.vertex_shader = nullptr; }
        }
        for (auto& lr : g_runtime.luts) {
            if (lr.texture) { SDL_ReleaseGPUTexture(g_runtime.device, lr.texture); lr.texture = nullptr; }
            if (lr.sampler) { SDL_ReleaseGPUSampler(g_runtime.device, lr.sampler); lr.sampler = nullptr; }
        }
        for (auto& t : g_runtime.prev_ring) {
            if (t) { SDL_ReleaseGPUTexture(g_runtime.device, t); t = nullptr; }
        }
        g_runtime.prev_head = 0;
        g_runtime.prev_ring_w = g_runtime.prev_ring_h = 0;
    }
    g_runtime.luts.clear();
#endif
    g_runtime.passes.clear();
    g_runtime.preset = PortGlslp::Preset{};
    g_runtime.loaded = false;
}

#ifdef TMC_GPU_RENDERER
/* Per-frame uniform data matching the LibretroUniforms block in
 * the preprocessor's stage headers. std140 layout: vec2 has base
 * alignment 8 (rule 2: 2N) AND size 8 — it does NOT pad to 16.
 * Only vec3 has the "stored as 12, aligned as 16" weirdness.
 * Verified with `spirv-dis`: glslang emits Offsets 0, 8, 16, 24, 28,
 * 32, 40 — tightly packed 8-byte vec2 + 4-byte int/uint slots. Total
 * size: 48 bytes. */
struct LibretroUniformsPacked {
    float    OutputSize[2];        /* offset 0  — vec2 */
    float    TextureSize[2];       /* offset 8  — vec2 */
    float    InputSize[2];         /* offset 16 — vec2 */
    uint32_t FrameCount;           /* offset 24 — uint */
    int32_t  FrameDirection;       /* offset 28 — int */
    float    OriginalSize[2];      /* offset 32 — vec2 */
    float    FinalViewportSize[2]; /* offset 40 — vec2 */
};
static_assert(sizeof(LibretroUniformsPacked) == 48,
              "std140 layout mismatch — must be 48 bytes");

/* Step 5 present — walks every pass in the loaded preset. Source for
 * pass 0 is `src_texture` (the GBA framebuffer); for pass i > 0 it's
 * the previous pass's out_texture. Last pass renders into
 * `swap_texture`. Caller is responsible for the surrounding command-
 * buffer / acquire / submit / ImGui-overlay scaffolding. */
extern "C" bool Port_GlslpRuntime_PresentFrame(SDL_GPUCommandBuffer* cmd,
                                               SDL_GPUTexture*       src_texture,
                                               SDL_GPUTexture*       swap_texture,
                                               int                   swap_w,
                                               int                   swap_h,
                                               int                   src_w,
                                               int                   src_h,
                                               uint32_t              frame_counter) {
    if (!g_runtime.loaded || g_runtime.passes.empty()) return false;
    if (!cmd || !src_texture || !swap_texture) return false;

    /* Window-resize: when the swapchain size changes (user resized
     * the window), any pass with scale_type=viewport now has a
     * stale-sized intermediate texture. Rebuild only those —
     * pipelines stay valid because their target format is unchanged.
     * source-scale and absolute-scale passes are independent of
     * viewport size; they keep their textures across resizes.
     *
     * Cheap to do every frame because the equality check skips out
     * fast in the common steady-state. */
    if ((Uint32)swap_w != g_runtime.assumed_vp_w
        || (Uint32)swap_h != g_runtime.assumed_vp_h) {
        for (size_t i = 0; i + 1 < g_runtime.passes.size(); ++i) {
            const auto& pdef = g_runtime.preset.passes[i];
            auto&       p    = g_runtime.passes[i];
            const bool viewport_scaled =
                pdef.scale_type_x == PortGlslp::ScaleType::Viewport ||
                pdef.scale_type_y == PortGlslp::ScaleType::Viewport;
            if (!viewport_scaled) continue;

            int new_w, new_h;
            ResolvePassSize(pdef, src_w, src_h, swap_w, swap_h, new_w, new_h);
            if (new_w == p.out_w && new_h == p.out_h) continue;

            if (p.out_texture) SDL_ReleaseGPUTexture(g_runtime.device, p.out_texture);
            SDL_GPUTextureCreateInfo tci = {};
            tci.type   = SDL_GPU_TEXTURETYPE_2D;
            tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            tci.width  = (Uint32)new_w;
            tci.height = (Uint32)new_h;
            tci.layer_count_or_depth = 1;
            tci.num_levels           = 1;
            p.out_texture = SDL_CreateGPUTexture(g_runtime.device, &tci);
            p.out_w = new_w; p.out_h = new_h;
        }
        g_runtime.assumed_vp_w = (Uint32)swap_w;
        g_runtime.assumed_vp_h = (Uint32)swap_h;
    }

    /* Lazy frame-history ring allocation. Match src dimensions; if
     * those change (rare — only if the GBA framebuffer ever resizes,
     * which it doesn't), reallocate the whole ring. After alloc, copy
     * the current frame's input into ring[prev_head]. The copy uses
     * SDL_BlitGPUTexture under the hood — cheap (same dimensions, no
     * resize, no format change). */
    if (g_runtime.prev_ring_w != src_w || g_runtime.prev_ring_h != src_h) {
        for (auto& t : g_runtime.prev_ring) {
            if (t) { SDL_ReleaseGPUTexture(g_runtime.device, t); t = nullptr; }
        }
        g_runtime.prev_ring_w = src_w;
        g_runtime.prev_ring_h = src_h;
        for (auto& t : g_runtime.prev_ring) {
            SDL_GPUTextureCreateInfo tci = {};
            tci.type   = SDL_GPU_TEXTURETYPE_2D;
            tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            tci.width  = (Uint32)src_w;
            tci.height = (Uint32)src_h;
            tci.layer_count_or_depth = 1;
            tci.num_levels           = 1;
            t = SDL_CreateGPUTexture(g_runtime.device, &tci);
        }
        g_runtime.prev_head = 0;
    }
    if (g_runtime.prev_ring[g_runtime.prev_head]) {
        SDL_GPUBlitInfo bi = {};
        bi.source.texture       = src_texture;
        bi.source.w             = (Uint32)src_w;
        bi.source.h             = (Uint32)src_h;
        bi.destination.texture  = g_runtime.prev_ring[g_runtime.prev_head];
        bi.destination.w        = (Uint32)src_w;
        bi.destination.h        = (Uint32)src_h;
        bi.load_op              = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter               = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &bi);
    }

    SDL_GPUTexture* prev_input = src_texture;
    int             prev_w     = src_w;
    int             prev_h     = src_h;

    for (size_t i = 0; i < g_runtime.passes.size(); ++i) {
        const auto& pdef = g_runtime.preset.passes[i];
        auto&       p    = g_runtime.passes[i];
        const bool  is_last = (i == g_runtime.passes.size() - 1);

        SDL_GPUTexture* target = is_last ? swap_texture : p.out_texture;
        const int       tw     = is_last ? swap_w : p.out_w;
        const int       th     = is_last ? swap_h : p.out_h;

        SDL_GPUColorTargetInfo color = {};
        color.texture     = target;
        color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        color.load_op     = SDL_GPU_LOADOP_CLEAR;
        color.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
        SDL_GPUViewport vp = {};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.w = (float)tw; vp.h = (float)th;
        vp.min_depth = 0.0f; vp.max_depth = 1.0f;
        SDL_SetGPUViewport(rp, &vp);

        SDL_BindGPUGraphicsPipeline(rp, p.pipeline);

        /* Primary input sampler + each LUT in source order. The
         * total binding array is { primary, LUT[0], LUT[1], ... } at
         * fragment set=2, binding=0..N (the preprocessor assigned
         * sequential bindings). */
        std::vector<SDL_GPUTextureSamplerBinding> samplers;
        samplers.reserve(1 + p.lut_names.size());
        SDL_GPUTextureSamplerBinding primary = {};
        primary.texture = prev_input;
        primary.sampler = pdef.filter_linear ? g_runtime.sampler_linear
                                              : g_runtime.sampler_nearest;
        samplers.push_back(primary);
        for (const auto& lut_name : p.lut_names) {
            SDL_GPUTextureSamplerBinding sb = {};
            /* First: does this sampler name match a named-alias of an
             * EARLIER pass? `aliasN = "Foo"` in the preset; any later
             * pass that declares `uniform sampler2D Foo;` gets that
             * pass's output bound here. The preprocessor records the
             * name in lut_sampler_names; we resolve to the right
             * texture at bind time. */
            for (size_t j = 0; j < i; ++j) {
                const auto& q = g_runtime.preset.passes[j];
                if (q.alias && *q.alias == lut_name) {
                    sb.texture = g_runtime.passes[j].out_texture;
                    sb.sampler = q.filter_linear ? g_runtime.sampler_linear
                                                  : g_runtime.sampler_nearest;
                    break;
                }
            }
            /* Feedback variant: `<alias>Feedback` samples that pass's
             * output from the PREVIOUS frame (prev_texture). Marked
             * up at Load; texture allocated only when needed. The
             * referenced pass may be at any position in the chain
             * (feedback can reference any aliased pass, including the
             * current one — its prev_texture is its last-frame output). */
            if (!sb.texture) {
                const std::string kSuffix = "Feedback";
                if (lut_name.size() > kSuffix.size()
                    && lut_name.compare(lut_name.size() - kSuffix.size(),
                                        kSuffix.size(), kSuffix) == 0) {
                    std::string alias = lut_name.substr(
                        0, lut_name.size() - kSuffix.size());
                    for (size_t j = 0; j < g_runtime.preset.passes.size(); ++j) {
                        const auto& q = g_runtime.preset.passes[j];
                        if (q.alias && *q.alias == alias) {
                            if (g_runtime.passes[j].prev_texture) {
                                sb.texture = g_runtime.passes[j].prev_texture;
                                sb.sampler = q.filter_linear
                                                ? g_runtime.sampler_linear
                                                : g_runtime.sampler_nearest;
                            }
                            break;
                        }
                    }
                }
            }
            /* Special pass-history identifiers: `Original` is pass-0
             * input (== the GBA framebuffer), `Source` is the previous
             * pass's output (already bound as primary; bind again).
             * `PassPrev<N>Texture` is the output of the pass N positions
             * earlier in the chain — used by crt-easymode-halation,
             * crt-royale, and others that bypass adjacent samplers. */
            if (!sb.texture) {
                if (lut_name == "Original" || lut_name == "OriginalTexture") {
                    sb.texture = src_texture;
                    sb.sampler = g_runtime.sampler_linear;
                } else if (lut_name == "Source" || lut_name == "SourceTexture") {
                    sb.texture = prev_input;
                    sb.sampler = g_runtime.sampler_linear;
                } else if (lut_name == "Prev" || lut_name == "PrevTexture"
                           || (lut_name.size() > 4 + 7
                               && lut_name.compare(0, 4, "Prev") == 0
                               && lut_name.compare(lut_name.size() - 7, 7, "Texture") == 0
                               && lut_name[4] >= '0' && lut_name[4] <= '9')) {
                    /* Prev / PrevTexture is 1 frame ago; PrevNTexture is
                     * N+1 frames ago (so Prev1Texture = 2 frames ago,
                     * Prev6Texture = 7 frames ago, the maximum). */
                    int n = 1;  /* default: 1 frame ago */
                    if (lut_name.size() > 4 + 7
                        && lut_name[4] >= '0' && lut_name[4] <= '9') {
                        n = 0;
                        for (size_t k = 4; k + 7 < lut_name.size(); ++k) {
                            if (lut_name[k] < '0' || lut_name[k] > '9') {
                                n = -1; break;
                            }
                            n = n * 10 + (lut_name[k] - '0');
                        }
                        if (n >= 0) n += 1;  /* Prev0 = 1 frame ago */
                    }
                    /* n < kPrevCount, not <=: the ring holds the current frame
                     * at prev_head plus (kPrevCount-1) past frames, so n==kPrevCount
                     * would alias prev_head (the current frame) instead of the
                     * oldest history slot. */
                    if (n > 0 && n < Runtime::kPrevCount) {
                        int slot = (g_runtime.prev_head + Runtime::kPrevCount - n)
                                   % Runtime::kPrevCount;
                        if (g_runtime.prev_ring[slot]) {
                            sb.texture = g_runtime.prev_ring[slot];
                            sb.sampler = g_runtime.sampler_linear;
                        }
                    }
                } else if (lut_name.size() > 8 + 7  /* "PassPrev"(8) + N + "Texture"(7) */
                           && lut_name.compare(0, 8, "PassPrev") == 0
                           && lut_name.compare(lut_name.size() - 7, 7, "Texture") == 0) {
                    int n = 0;
                    for (size_t k = 8; k + 7 < lut_name.size(); ++k) {
                        if (lut_name[k] < '0' || lut_name[k] > '9') { n = -1; break; }
                        n = n * 10 + (lut_name[k] - '0');
                    }
                    if (n > 0 && (size_t)n <= i) {
                        sb.texture = g_runtime.passes[i - n].out_texture;
                        sb.sampler = g_runtime.sampler_linear;
                    } else if (n > 0) {
                        /* Requested earlier pass than exists — fall back
                         * to the original input. */
                        sb.texture = src_texture;
                        sb.sampler = g_runtime.sampler_linear;
                    }
                }
            }
            /* Otherwise, treat as an actual LUT texture. */
            if (!sb.texture) {
                for (const auto& lr : g_runtime.luts) {
                    if (lr.name == lut_name) {
                        sb.texture = lr.texture;
                        sb.sampler = lr.sampler ? lr.sampler : g_runtime.sampler_linear;
                        break;
                    }
                }
            }
            /* Missing slot (no alias, no LUT) — bind any texture so the
             * draw is valid. Prefer the magenta placeholder if present;
             * otherwise reuse the primary input. */
            if (!sb.texture && !g_runtime.luts.empty()) {
                sb.texture = g_runtime.luts.front().texture;
                sb.sampler = g_runtime.luts.front().sampler ? g_runtime.luts.front().sampler
                                                              : g_runtime.sampler_linear;
            }
            if (!sb.texture) {
                sb.texture = prev_input;
                sb.sampler = g_runtime.sampler_linear;
            }
            samplers.push_back(sb);
        }
        SDL_BindGPUFragmentSamplers(rp, 0, samplers.data(), (Uint32)samplers.size());

        /* LibretroUniforms — same data pushed to both stages, since
         * the preprocessor's headers declare an identical block in
         * vert and frag. */
        LibretroUniformsPacked u = {};
        u.OutputSize[0]        = (float)tw;       u.OutputSize[1]        = (float)th;
        u.TextureSize[0]       = (float)prev_w;   u.TextureSize[1]       = (float)prev_h;
        u.InputSize[0]         = (float)prev_w;   u.InputSize[1]         = (float)prev_h;
        u.FrameCount           = frame_counter;
        u.FrameDirection       = 1;
        u.OriginalSize[0]      = (float)src_w;    u.OriginalSize[1]      = (float)src_h;
        u.FinalViewportSize[0] = (float)swap_w;   u.FinalViewportSize[1] = (float)swap_h;
        SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));

        /* ShaderParams — the per-pass uniform block matching the
         * `layout(set = 3, binding = 1) uniform ShaderParams { ... }`
         * the preprocessor emitted. Fields are in #pragma-parameter
         * declaration order (p.params); we look up each by ID in the
         * preset's overrides and fall back to the parameter's
         * compiled-in default.
         *
         * std140 layout: a UB of plain floats packs tightly (4-byte
         * stride). The earlier "each float gets a vec4 slot" comment
         * was wrong — that rule only kicks in for vec3 and arrays.
         * Confirmed via spirv-dis: glslang emits Offset 0,4,8,12,...
         * for CRT-Geom's 18 sequential floats. */
        if (!p.params.empty()) {
            const size_t n = p.params.size();
            std::vector<float> packed(n, 0.0f);
            for (size_t k = 0; k < n; ++k) {
                float v = p.params[k].default_value;
                for (const auto& pp : g_runtime.preset.parameters) {
                    if (pp.id == p.params[k].id) { v = pp.current_value; break; }
                }
                packed[k] = v;
            }
            SDL_PushGPUFragmentUniformData(cmd, 1, packed.data(),
                                            (Uint32)(packed.size() * sizeof(float)));
            /* Vertex stage also needs the params — libretro vertex
             * shaders precompute things like CRT-Geom's stretch / one /
             * mod_factor that use #pragma-parameter knobs. */
            SDL_PushGPUVertexUniformData(cmd, 1, packed.data(),
                                          (Uint32)(packed.size() * sizeof(float)));
        }

        SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
        SDL_EndGPURenderPass(rp);

        if (!is_last) {
            prev_input = p.out_texture;
            prev_w     = p.out_w;
            prev_h     = p.out_h;
        }
    }

    /* End-of-frame feedback swap. For every pass with a feedback alias,
     * swap current↔previous so the NEXT frame samples THIS frame's
     * output via `<alias>Feedback`. Cheap pointer swap; no GPU copy. */
    for (auto& p : g_runtime.passes) {
        if (!p.needs_feedback) continue;
        SDL_GPUTexture* tmp = p.prev_texture;
        p.prev_texture = p.out_texture;
        p.out_texture  = tmp;
    }
    /* Advance the prev-ring head so next frame's copy lands in a
     * fresh slot. The slot we just wrote becomes "PrevTexture" for
     * the next frame (1 frame ago); the one before becomes
     * "Prev1Texture" (2 frames ago); etc. */
    g_runtime.prev_head = (g_runtime.prev_head + 1) % Runtime::kPrevCount;
    return true;
}
#endif  /* TMC_GPU_RENDERER */

extern "C" int Port_GlslpRuntime_IsActive(void) {
    /* A preset with zero usable passes must not register as active, or the
     * present path would route to it and overlay ImGui on an undefined
     * swapchain. (The parser already rejects shaders<1; this is defense.) */
    return (g_runtime.loaded && !g_runtime.passes.empty()) ? 1 : 0;
}
