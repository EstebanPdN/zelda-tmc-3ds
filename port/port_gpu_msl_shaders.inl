/*
 * Stock SDL_GPU shader filters in Metal Shading Language.
 *
 * Included by port_gpu_renderer.cpp on Apple builds. The GLSL/SPIR-V
 * versions under port/shaders/ remain the source of truth for Vulkan;
 * keep these functions behaviorally identical when changing a stock
 * filter. Binding order follows SDL_GPU's MSL convention:
 *   texture(0), sampler(0), then uniform buffer(0).
 */

static const char kPassthroughVertMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

vertex RasterizerData main0(uint vertexID [[vertex_id]]) {
    const bool right = (vertexID & 1u) != 0u;
    const bool top   = (vertexID & 2u) != 0u;

    RasterizerData out;
    out.position = float4(right ? 1.0 : -1.0,
                          top   ? 1.0 : -1.0,
                          0.0,
                          1.0);
    out.texCoord = float2(right ? 1.0 : 0.0,
                          top   ? 0.0 : 1.0);
    return out;
}
)msl";

static const char kPassthroughFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]]) {
    return uSource.sample(uSampler, in.texCoord);
}
)msl";

static const char kLcdGridFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float4 c = uSource.sample(uSampler, in.texCoord);
    const float2 cellStride = params.uViewportSize / float2(240.0, 160.0);
    const float2 cellPos = fmod(in.position.xy, cellStride);
    const bool border = (cellPos.x < 1.0) || (cellPos.y < 1.0);
    const float dim = border ? (180.0 / 255.0) : 1.0;
    return float4(c.rgb * dim, c.a);
}
)msl";

static const char kScanlineFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float4 c = uSource.sample(uSampler, in.texCoord);
    const float rowStride = params.uViewportSize.y / 160.0;
    const uint row = uint(in.position.y / max(rowStride, 1.0));
    const float dim = ((row & 1u) != 0u) ? (192.0 / 255.0) : 1.0;
    return float4(c.rgb * dim, c.a);
}
)msl";

static const char kHandheldFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float4 c = uSource.sample(uSampler, in.texCoord);
    const float3 tinted = c.rgb * float3(240.0 / 256.0,
                                         264.0 / 256.0,
                                         232.0 / 256.0);
    const float2 cellStride = params.uViewportSize / float2(240.0, 160.0);
    const float2 cellPos = fmod(in.position.xy, cellStride);
    const bool border = (cellPos.x < 1.0) || (cellPos.y < 1.0);
    const float dim = border ? (154.0 / 255.0) : 1.0;
    return float4(clamp(tinted * dim, float3(0.0), float3(1.0)), c.a);
}
)msl";

static const char kVignetteFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float4 c = uSource.sample(uSampler, in.texCoord);
    const float2 centre = params.uViewportSize * 0.5;
    const float2 delta = in.position.xy - centre;
    const float d2 = dot(delta, delta);
    const float maxD2 = dot(centre, centre);
    const float inner = maxD2 * 0.36;
    const float span = maxD2 - inner;

    float gain = 1.0;
    if (d2 > inner) {
        const float t = clamp((d2 - inner) / span, 0.0, 1.0);
        const float cornerGain = 141.0 / 256.0;
        gain = 1.0 + (cornerGain - 1.0) * t;
    }
    return float4(c.rgb * gain, c.a);
}
)msl";

static const char kCrtCompositeFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

static float3 blur3(texture2d<float> src, sampler smp, float2 uv) {
    const float2 dx = float2(1.0 / 240.0, 0.0);
    const float3 l = src.sample(smp, uv - dx).rgb;
    const float3 c = src.sample(smp, uv).rgb;
    const float3 r = src.sample(smp, uv + dx).rgb;
    return (l + 2.0 * c + r) * 0.25;
}

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float3 c = blur3(uSource, uSampler, in.texCoord);
    const float stripeW = max(params.uViewportSize.x / 240.0, 1.0);
    const uint stripe = uint(in.position.x / stripeW) % 3u;

    float3 gain;
    if (stripe == 0u) {
        gain = float3(320.0, 184.0, 184.0) / 256.0;
    } else if (stripe == 1u) {
        gain = float3(184.0, 320.0, 184.0) / 256.0;
    } else {
        gain = float3(184.0, 184.0, 320.0) / 256.0;
    }

    gain.r *= 268.0 / 256.0;
    gain.b *= 235.0 / 256.0;
    return float4(clamp(c * gain, float3(0.0), float3(1.0)), 1.0);
}
)msl";

static const char kBlur5hFragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]]) {
    const float2 dx = float2(1.0 / 240.0, 0.0);
    const float3 t0 = uSource.sample(uSampler, in.texCoord - dx - dx).rgb;
    const float3 t1 = uSource.sample(uSampler, in.texCoord - dx).rgb;
    const float3 t2 = uSource.sample(uSampler, in.texCoord).rgb;
    const float3 t3 = uSource.sample(uSampler, in.texCoord + dx).rgb;
    const float3 t4 = uSource.sample(uSampler, in.texCoord + dx + dx).rgb;
    const float3 c = (t0 + 2.0 * t1 + 3.0 * t2 + 2.0 * t3 + t4) / 9.0;
    return float4(c, 1.0);
}
)msl";

static const char kCrtRfP2FragMsl[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct FragParams {
    float2 uViewportSize;
    float2 _pad;
};

fragment float4 main0(RasterizerData in [[stage_in]],
                      texture2d<float> uSource [[texture(0)]],
                      sampler uSampler [[sampler(0)]],
                      constant FragParams& params [[buffer(0)]]) {
    const float3 c = uSource.sample(uSampler, in.texCoord).rgb;
    const float stripeW = max(params.uViewportSize.x / 240.0, 1.0);
    const uint stripe = uint(in.position.x / stripeW) % 3u;

    float3 gain;
    if (stripe == 0u) {
        gain = float3(320.0, 184.0, 184.0) / 256.0;
    } else if (stripe == 1u) {
        gain = float3(184.0, 320.0, 184.0) / 256.0;
    } else {
        gain = float3(184.0, 184.0, 320.0) / 256.0;
    }

    gain.r *= 271.0 / 256.0;
    gain.b *= 230.0 / 256.0;

    const float rowStride = max(params.uViewportSize.y / 160.0, 1.0);
    const uint row = uint(in.position.y / rowStride);
    if ((row & 1u) != 0u) {
        gain *= 205.0 / 256.0;
    }

    return float4(clamp(c * gain, float3(0.0), float3(1.0)), 1.0);
}
)msl";

static constexpr size_t kPassthroughVertMslSize  = sizeof(kPassthroughVertMsl) - 1;
static constexpr size_t kPassthroughFragMslSize  = sizeof(kPassthroughFragMsl) - 1;
static constexpr size_t kLcdGridFragMslSize      = sizeof(kLcdGridFragMsl) - 1;
static constexpr size_t kScanlineFragMslSize     = sizeof(kScanlineFragMsl) - 1;
static constexpr size_t kHandheldFragMslSize     = sizeof(kHandheldFragMsl) - 1;
static constexpr size_t kVignetteFragMslSize     = sizeof(kVignetteFragMsl) - 1;
static constexpr size_t kCrtCompositeFragMslSize = sizeof(kCrtCompositeFragMsl) - 1;
static constexpr size_t kBlur5hFragMslSize       = sizeof(kBlur5hFragMsl) - 1;
static constexpr size_t kCrtRfP2FragMslSize      = sizeof(kCrtRfP2FragMsl) - 1;
