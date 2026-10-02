/*
 * port_glslp_parser.cpp — Stage 5+C step 1: INI tokenizer + raw
 * key/value map.
 *
 * This is the first concrete piece of the .glslp runtime described
 * in port_glslp_parser.h. Subsequent steps (Preset assembly, libretro
 * GLSL preprocessor, multi-pass render graph, LUT loader) build on
 * top of the key/value map this file produces.
 *
 * Scope of this step:
 *   - Read a .glslp file from disk.
 *   - Strip comments (# and ;).
 *   - Strip whitespace.
 *   - Parse `key = value` lines into a flat std::map<string, string>.
 *   - Unquote string values ("..." with backslash escapes).
 *   - Return nullopt on syntactic errors with a stderr diagnostic.
 *
 * Out of scope here (left for next steps):
 *   - Semantic assembly into PortGlslp::Preset (turn shader0/...
 *     keys into ShaderPass[]).
 *   - Path resolution (relative paths get the source-file dirname
 *     joined later, in the Preset assembler).
 *   - INI section headers — libretro .glslp files don't use them.
 */

#include "port_glslp_parser.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace PortGlslp {

namespace {

/* Drop leading whitespace + comments (# / ;) from `s`. */
static std::string Trim(const std::string& s) {
    size_t lo = 0;
    while (lo < s.size() && std::isspace((unsigned char)s[lo])) ++lo;
    size_t hi = s.size();
    while (hi > lo && std::isspace((unsigned char)s[hi - 1])) --hi;
    return s.substr(lo, hi - lo);
}

/* Strip a trailing comment from `line`. Comments start at # or ;
 * outside of a quoted string. The libretro spec doesn't formally
 * specify quote-aware comments but several presets in the wild use
 * # inside paths (rare but possible), so we honour quoting. */
static std::string StripComment(const std::string& line) {
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_quote = !in_quote;
        } else if (!in_quote && (c == '#' || c == ';')) {
            return line.substr(0, i);
        }
    }
    return line;
}

/* Unquote a value: if it starts AND ends with a double quote, strip
 * them and process the standard JSON-style escapes (\" \\ \n \t).
 * Otherwise return unchanged. */
static std::string Unquote(const std::string& s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
    std::string out;
    out.reserve(s.size() - 2);
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 2 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case '"':  out += '"';  ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += c;   /* keep the backslash literal */
            }
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

/* Tokenize one .glslp file into key/value pairs. Returns an empty
 * map AND prints to stderr on error so the caller can distinguish
 * "no file" from "valid empty file" by inspecting `*ok` if needed.
 * For now: empty return on any failure; diagnostics go to stderr. */
std::map<std::string, std::string> TokenizeGlslpFile(const char* path) {
    std::map<std::string, std::string> out;

    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[glslp] open failed: %s\n", path);
        return out;
    }

    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        /* Strip the BOM SDL/Notepad sometimes appends — UTF-8 BOM is
         * EF BB BF, present at byte 0 of line 1 only. */
        if (lineno == 1 && line.size() >= 3
            && (unsigned char)line[0] == 0xEF
            && (unsigned char)line[1] == 0xBB
            && (unsigned char)line[2] == 0xBF) {
            line.erase(0, 3);
        }

        std::string s = StripComment(line);
        s = Trim(s);
        if (s.empty()) continue;

        /* Find the `=`. Spaces around it are optional but common. */
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "[glslp] %s:%d: missing '=' in '%s'\n",
                         path, lineno, s.c_str());
            continue;
        }
        std::string key = Trim(s.substr(0, eq));
        std::string val = Trim(s.substr(eq + 1));
        if (key.empty()) {
            std::fprintf(stderr, "[glslp] %s:%d: empty key\n", path, lineno);
            continue;
        }
        out[key] = Unquote(val);
    }
    return out;
}

/* ----- Step 2 helpers ---------------------------------------------- */

namespace {

/* Look up a key with a default fallback. Returns the default when
 * the key is absent so per-pass options collapse cleanly to defaults. */
static const std::string* Find(const std::map<std::string, std::string>& m,
                               const std::string& key) {
    auto it = m.find(key);
    return (it == m.end()) ? nullptr : &it->second;
}

static int ParseInt(const std::string& s, int dflt) {
    try { return std::stoi(s); } catch (...) { return dflt; }
}
static float ParseFloat(const std::string& s, float dflt) {
    try { return std::stof(s); } catch (...) { return dflt; }
}
static bool ParseBool(const std::string& s, bool dflt) {
    if (s.empty()) return dflt;
    if (s == "true"  || s == "True"  || s == "TRUE"  || s == "1" || s == "yes") return true;
    if (s == "false" || s == "False" || s == "FALSE" || s == "0" || s == "no")  return false;
    return dflt;
}

static ScaleType ParseScaleType(const std::string& s) {
    if (s == "source")   return ScaleType::Source;
    if (s == "viewport") return ScaleType::Viewport;
    if (s == "absolute") return ScaleType::Absolute;
    return ScaleType::Source;  /* libretro default */
}

static WrapMode ParseWrapMode(const std::string& s) {
    if (s == "clamp_to_border")  return WrapMode::ClampBorder;
    if (s == "clamp_to_edge")    return WrapMode::ClampEdge;
    if (s == "repeat")           return WrapMode::Repeat;
    if (s == "mirrored_repeat")  return WrapMode::MirroredRepeat;
    return WrapMode::ClampBorder;  /* libretro default */
}

/* Split "A;B;C" into ["A", "B", "C"]. Empty tokens dropped. */
static std::vector<std::string> SplitSemicolons(const std::string& s) {
    std::vector<std::string> out;
    size_t lo = 0;
    while (lo <= s.size()) {
        size_t hi = s.find(';', lo);
        if (hi == std::string::npos) hi = s.size();
        std::string tok = s.substr(lo, hi - lo);
        /* Trim per-token whitespace — libretro presets sometimes write
         * "A; B; C" with spaces. */
        size_t ts = tok.find_first_not_of(" \t");
        size_t te = tok.find_last_not_of(" \t");
        if (ts != std::string::npos) {
            out.push_back(tok.substr(ts, te - ts + 1));
        }
        lo = hi + 1;
    }
    return out;
}

/* Resolve a maybe-relative path against the .glslp file's dirname.
 * Absolute paths return unchanged. */
static std::string ResolvePath(const std::string& glslp_path,
                               const std::string& rel) {
    namespace fs = std::filesystem;
    fs::path p(rel);
    if (p.is_absolute()) return p.lexically_normal().string();
    fs::path base = fs::path(glslp_path).parent_path();
    return (base / p).lexically_normal().string();
}

}  // namespace

/* Step 2 — assemble the raw key/value map from Step 1 into a Preset.
 * Catches malformed pass counts, missing required keys, and missing
 * referenced files (warn-but-continue, so partial trees still load). */
std::optional<Preset> LoadPresetFile(const char* glslp_path) {
    if (!glslp_path) return std::nullopt;

    auto map = TokenizeGlslpFile(glslp_path);
    if (map.empty()) {
        /* TokenizeGlslpFile already logged the reason. */
        return std::nullopt;
    }

    Preset out;
    out.source_path = glslp_path;

    /* Pass count (`shaders = N`). Required. */
    const std::string* shaders_str = Find(map, "shaders");
    if (!shaders_str) {
        std::fprintf(stderr, "[glslp] %s: missing required key 'shaders'\n", glslp_path);
        return std::nullopt;
    }
    int npasses = ParseInt(*shaders_str, -1);
    if (npasses < 1 || npasses > 64) {  /* must have >=1 pass; libretro caps ~16 */
        std::fprintf(stderr, "[glslp] %s: bad shaders count %d (need 1..64)\n", glslp_path, npasses);
        return std::nullopt;
    }

    /* Per-pass keys. libretro accepts both unified `scale_typeN/scaleN`
     * and per-axis `scale_type_xN/scale_xN` forms; prefer the per-axis
     * keys when present, fall back to the unified key for both axes. */
    out.passes.reserve(npasses);
    for (int i = 0; i < npasses; ++i) {
        ShaderPass p;
        const std::string idx = std::to_string(i);

        const std::string* shader = Find(map, "shader" + idx);
        if (!shader) {
            std::fprintf(stderr, "[glslp] %s: pass %d missing 'shader%d' key\n",
                         glslp_path, i, i);
            return std::nullopt;
        }
        p.path = ResolvePath(glslp_path, *shader);

        if (const std::string* v = Find(map, "filter_linear" + idx))    p.filter_linear    = ParseBool(*v, false);
        if (const std::string* v = Find(map, "wrap_mode" + idx))        p.wrap             = ParseWrapMode(*v);
        if (const std::string* v = Find(map, "mipmap_input" + idx))     p.mipmap_input     = ParseBool(*v, false);
        if (const std::string* v = Find(map, "srgb_framebuffer" + idx)) p.srgb_framebuffer = ParseBool(*v, false);
        if (const std::string* v = Find(map, "float_framebuffer" + idx)) p.float_framebuffer = ParseBool(*v, false);

        /* Scale type/factor: per-axis takes precedence over unified. */
        ScaleType st_unified = ScaleType::Source;
        float     sc_unified = 1.0f;
        if (const std::string* v = Find(map, "scale_type" + idx)) st_unified = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale" + idx))      sc_unified = ParseFloat(*v, 1.0f);
        p.scale_type_x = st_unified;
        p.scale_type_y = st_unified;
        p.scale_x = sc_unified;
        p.scale_y = sc_unified;
        if (const std::string* v = Find(map, "scale_type_x" + idx)) p.scale_type_x = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale_type_y" + idx)) p.scale_type_y = ParseScaleType(*v);
        if (const std::string* v = Find(map, "scale_x" + idx))      p.scale_x      = ParseFloat(*v, p.scale_x);
        if (const std::string* v = Find(map, "scale_y" + idx))      p.scale_y      = ParseFloat(*v, p.scale_y);

        if (const std::string* v = Find(map, "alias" + idx)) {
            if (!v->empty()) p.alias = *v;
        }

        /* Soft-validate the shader file exists. Missing-but-continue
         * so a partial extractor output doesn't kill load — Step 5
         * (render-graph executor) will fail loudly if the shader
         * can't be compiled. */
        std::error_code ec;
        if (!std::filesystem::exists(p.path, ec)) {
            std::fprintf(stderr, "[glslp] %s: pass %d shader missing: %s\n",
                         glslp_path, i, p.path.c_str());
        }

        out.passes.push_back(std::move(p));
    }

    /* Lookup-textures (LUTs). `textures = "A;B;..."` then per-name
     * keys. Each LUT's path is the `NAME = path` line. */
    if (const std::string* tex_list = Find(map, "textures")) {
        for (const auto& name : SplitSemicolons(*tex_list)) {
            const std::string* path = Find(map, name);
            if (!path) {
                std::fprintf(stderr, "[glslp] %s: LUT '%s' listed in textures= but no '%s = ...' line\n",
                             glslp_path, name.c_str(), name.c_str());
                continue;
            }
            LutTexture lut;
            lut.name = name;
            lut.path = ResolvePath(glslp_path, *path);
            if (const std::string* v = Find(map, name + "_linear"))    lut.linear = ParseBool(*v, true);
            if (const std::string* v = Find(map, name + "_wrap_mode")) lut.wrap   = ParseWrapMode(*v);
            if (const std::string* v = Find(map, name + "_mipmap"))    lut.mipmap = ParseBool(*v, false);

            std::error_code ec;
            if (!std::filesystem::exists(lut.path, ec)) {
                std::fprintf(stderr, "[glslp] %s: LUT '%s' file missing: %s\n",
                             glslp_path, name.c_str(), lut.path.c_str());
            }
            out.luts.push_back(std::move(lut));
        }
    }

    /* Runtime-tunable parameters declared in `parameters = "..."` and
     * the matching `paramName = value` overrides. The per-parameter
     * label/min/max/step come from `#pragma parameter` directives in
     * the GLSL source itself — that's Step 3 (preprocessor). For now
     * we only record the names + override values; Step 3 will fill in
     * the metadata when it walks the GLSL sources. */
    if (const std::string* param_list = Find(map, "parameters")) {
        for (const auto& id : SplitSemicolons(*param_list)) {
            ShaderParam pp;
            pp.id = id;
            pp.label = id;  /* placeholder; Step 3 reads `#pragma parameter` for real */
            if (const std::string* v = Find(map, id)) {
                pp.default_value = pp.current_value = ParseFloat(*v, 0.0f);
            }
            out.parameters.push_back(std::move(pp));
        }
    }

    /* Optional feedback pass. */
    if (const std::string* fb = Find(map, "feedback_pass")) {
        int fp = ParseInt(*fb, -1);
        if (fp >= 0 && fp < npasses) out.feedback_pass = fp;
    }

    return out;
}

/* ----- Step 4: glslangValidator wrapper + content-hash cache -------- */

namespace {

/* 64-bit FNV-1a hash over a string. Plenty for distinguishing shader
 * sources in the cache directory; the cache key is just a filename
 * tag, not a cryptographic identifier. */
static uint64_t Fnv1a64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

static bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto end = f.tellg();
    f.seekg(0);
    out.resize((size_t)end);
    if (!f.read(reinterpret_cast<char*>(out.data()), out.size())) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace

std::optional<std::vector<uint8_t>>
CompileGlslToSpirv(const std::string& glsl_source,
                   ShaderStage        stage,
                   const std::string& cache_dir) {
    namespace fs = std::filesystem;

    const char* stage_flag = (stage == ShaderStage::Vertex) ? "vert" : "frag";
    const std::string ext  = (stage == ShaderStage::Vertex) ? "vert.spv" : "frag.spv";

    /* Cache lookup. The hash incorporates the stage so a vertex
     * shader text that happens to also be valid fragment source
     * doesn't share a cache slot with its fragment build. */
    std::string cache_path;
    if (!cache_dir.empty()) {
        uint64_t h = Fnv1a64(glsl_source);
        h ^= ((stage == ShaderStage::Vertex) ? 0x1ULL : 0x2ULL) * 0x9e3779b97f4a7c15ULL;
        char namebuf[40];
        std::snprintf(namebuf, sizeof(namebuf), "%016llx.%s",
                      (unsigned long long)h, ext.c_str());
        std::error_code ec;
        fs::create_directories(cache_dir, ec);
        cache_path = (fs::path(cache_dir) / namebuf).string();

        std::vector<uint8_t> cached;
        if (ReadFileBytes(cache_path, cached) && !cached.empty()) {
            return cached;
        }
    }

    /* Write the GLSL to a temp file (glslangValidator only takes
     * paths, not stdin in a way that emits to stdout cleanly). */
    /* Create the input temp via mkstemp: O_EXCL + an unpredictable suffix, so
     * a pre-planted symlink in a shared temp dir can't redirect our write (the
     * old fixed "/tmp/tmc_glslp_in_<pid>" name was guessable). The .spv output
     * path derives from the unique input name. */
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir == nullptr || tmpdir[0] == '\0') tmpdir = "/tmp";
    char tmp_in_buf[256];
    std::snprintf(tmp_in_buf, sizeof(tmp_in_buf), "%s/tmc_glslp_%s_XXXXXX", tmpdir, stage_flag);
    int in_fd = mkstemp(tmp_in_buf);
    if (in_fd < 0) {
        std::fprintf(stderr, "[glslp] failed to create tmp shader file under %s\n", tmpdir);
        return std::nullopt;
    }
    char tmp_out_buf[264];
    std::snprintf(tmp_out_buf, sizeof(tmp_out_buf), "%s.spv", tmp_in_buf);
    {
        size_t off = 0;
        bool wrote = true;
        while (off < glsl_source.size()) {
            ssize_t n = ::write(in_fd, glsl_source.data() + off, glsl_source.size() - off);
            if (n <= 0) { wrote = false; break; }
            off += (size_t)n;
        }
        ::close(in_fd);
        if (!wrote) {
            std::fprintf(stderr, "[glslp] failed to write tmp shader %s\n", tmp_in_buf);
            std::remove(tmp_in_buf);
            return std::nullopt;
        }
    }

    /* Shell out to glslangValidator. -V = Vulkan SPIR-V output;
     * --quiet suppresses the "filename printed on success" line. */
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "glslangValidator -V -S %s -o %s %s 2>&1",
                  stage_flag, tmp_out_buf, tmp_in_buf);
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        std::fprintf(stderr, "[glslp] popen failed for glslangValidator\n");
        std::remove(tmp_in_buf);
        return std::nullopt;
    }
    std::string diag;
    char line[256];
    while (std::fgets(line, sizeof(line), pipe)) {
        diag += line;
    }
    int rc = pclose(pipe);

    std::vector<uint8_t> spv;
    bool ok = (rc == 0) && ReadFileBytes(tmp_out_buf, spv) && !spv.empty();

    std::remove(tmp_in_buf);
    std::remove(tmp_out_buf);

    if (!ok) {
        std::fprintf(stderr, "[glslp] glslang failed (rc=%d):\n%s\n", rc, diag.c_str());
        return std::nullopt;
    }

    /* Persist to cache for next launch. Best-effort: on failure to
     * write, log and continue (the in-memory result is still valid). */
    if (!cache_path.empty()) {
        std::ofstream f(cache_path, std::ios::binary);
        if (f) {
            f.write(reinterpret_cast<const char*>(spv.data()), spv.size());
        } else {
            std::fprintf(stderr, "[glslp] failed to write cache: %s\n", cache_path.c_str());
        }
    }
    return spv;
}

/* ----- Step 3: libretro GLSL preprocessor --------------------------- */

namespace {

/* Find/replace each occurrence of `from` with `to` in `s`. Avoids
 * pulling regex in just for whole-word lookups; we operate on
 * identifier-by-identifier substitution where simple find-and-
 * replace is correct. */
static void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

/* Read one whitespace-delimited token starting at `pos`. Updates `pos`
 * to the start of the NEXT token (i.e. past trailing whitespace). */
static std::string ReadToken(const std::string& line, size_t& pos) {
    while (pos < line.size() && std::isspace((unsigned char)line[pos])) ++pos;
    if (pos >= line.size()) return {};
    size_t start = pos;
    if (line[pos] == '"') {
        ++pos;
        while (pos < line.size() && line[pos] != '"') ++pos;
        if (pos < line.size()) ++pos;
        return line.substr(start + 1, pos - start - 2);
    }
    while (pos < line.size() && !std::isspace((unsigned char)line[pos])) ++pos;
    return line.substr(start, pos - start);
}

/* Parse one #pragma parameter line. libretro syntax:
 *   #pragma parameter ID "Label" default min max [step]
 * (step optional, defaults to (max-min)/100.) */
static std::optional<ShaderParam> ParsePragmaParameter(const std::string& line) {
    /* `line` is the trimmed remainder after the leading `#pragma parameter`
     * tokens have been consumed by the caller. */
    size_t pos = 0;
    ShaderParam p;
    p.id = ReadToken(line, pos);
    p.label = ReadToken(line, pos);
    if (p.id.empty() || p.label.empty()) return std::nullopt;
    std::string sd = ReadToken(line, pos);
    std::string sm = ReadToken(line, pos);
    std::string sx = ReadToken(line, pos);
    std::string ss = ReadToken(line, pos);
    if (sd.empty() || sm.empty() || sx.empty()) return std::nullopt;
    p.default_value = ParseFloat(sd, 0.0f);
    p.min_value     = ParseFloat(sm, 0.0f);
    p.max_value     = ParseFloat(sx, 1.0f);
    p.step_value    = ss.empty() ? (p.max_value - p.min_value) / 100.0f
                                 : ParseFloat(ss, 0.01f);
    p.current_value = p.default_value;
    return p;
}

/* GLSL 450 header injected at the top of both stage outputs. Matches
 * the binding layout port_gpu_renderer.cpp's pipelines already use
 * (sampler at set=2/binding=0, FragParams UBO at set=3/binding=0).
 * Standard libretro uniforms are declared once and the generated
 * shader body can reference them as plain identifiers — matches the
 * libretro source convention. */
/* Headers DON'T declare vTexCoord — the shader source itself provides
 * it via `COMPAT_VARYING vec2 TEX0;` which our COMPAT/TEX0 rewrites
 * turn into `out vec2 vTexCoord;` (vertex) / `in vec2 vTexCoord;`
 * (fragment). Declaring it in the header too would double-declare. */
static const char* kVertexHeader = R"GLSL(#version 450
/* Real libretro shaders gate their per-parameter uniform/default
 * declarations on PARAMETER_UNIFORM. Defining it here forces the
 * `#ifdef PARAMETER_UNIFORM` branch (which declares `uniform float
 * NAME;` — we strip these and replace with our ShaderParams block)
 * over the `#else #define NAME default` branch (which would redefine
 * the same names the preprocessor is already #defining itself). */
#define PARAMETER_UNIFORM
/* Libretro shaders that don't use #pragma stage markers gate vertex
 * vs fragment code with `#if defined(VERTEX)` / `#elif defined(FRAGMENT)`.
 * Define the appropriate one per stage so the right `void main()`
 * actually appears in the output. Harmless when the shader uses
 * #pragma stage markers instead. */
#define VERTEX
/* Only the non-varying COMPAT_* macros stay as #defines. COMPAT_VARYING
 * and COMPAT_ATTRIBUTE are rewritten per-line by RewriteVaryingDecls
 * with sequential layout(location = N) decorators (location 0 for the
 * first declared name, 1 for the second, etc.). */
#define COMPAT_TEXTURE   texture
#define COMPAT_PRECISION
/* SDL_GPU SPIR-V binding convention places VERTEX uniform buffers at
 * descriptor set 1 (and fragment uniform buffers at set 3). Using
 * set=3 here would read garbage from a fragment-stage slot — making
 * every vertex-stage uniform read in libretro shaders (OutputSize,
 * TextureSize, etc.) come back uninitialised. That's silent on most
 * passthrough shaders but catastrophic on anything that does math in
 * the vertex stage (CRT-Geom's mod_factor / one / stretch precompute,
 * crt-easymode's sample-grid offsets, etc.). */
layout(set = 1, binding = 0) uniform LibretroUniforms {
    vec2  OutputSize;
    vec2  TextureSize;
    vec2  InputSize;
    uint  FrameCount;
    int   FrameDirection;
    vec2  OriginalSize;
    vec2  FinalViewportSize;
} u;
/* Same set convention applies to the per-pass ShaderParams block —
 * vertex uses set 1, fragment uses set 3. The param_block_decl
 * (emitted by the preprocessor) refers to _GLSLP_PARAMS_SET. */
#define _GLSLP_PARAMS_SET 1
#define MVPMatrix (mat4(1.0))
/* Vertex-stage `#define` aliases mirror the fragment header so
 * libretro shaders that reference these names directly (without
 * the `u.` prefix) compile in both stages. */
#define OutputSize        u.OutputSize
#define TextureSize       u.TextureSize
#define InputSize         u.InputSize
#define FrameCount        int(u.FrameCount)
#define FrameDirection    u.FrameDirection
#define OriginalSize      u.OriginalSize
#define FinalViewportSize u.FinalViewportSize
/* The runtime draws a 4-vertex triangle strip without any vertex
 * buffer. The libretro vertex-shader convention is `in vec4 VertexCoord;`
 * for clip-space position and `in vec4 TexCoord;` for UVs — both of
 * which would be uninitialised garbage here. Synthesise them from
 * gl_VertexIndex: bit 0 = right/left, bit 1 = top/bottom. The
 * RewriteVaryingDecls pass strips the matching attribute declarations
 * from the source so these macros don't collide. */
vec4 _glslp_vertex_coord() {
    return vec4(
        (gl_VertexIndex & 1) == 0 ? -1.0 :  1.0,
        (gl_VertexIndex & 2) == 0 ? -1.0 :  1.0,
        0.0, 1.0);
}
vec4 _glslp_tex_coord() {
    return vec4(
        (gl_VertexIndex & 1) == 0 ?  0.0 :  1.0,
        (gl_VertexIndex & 2) == 0 ?  1.0 :  0.0,
        0.0, 0.0);
}
#define VertexCoord  _glslp_vertex_coord()
#define TexCoord     _glslp_tex_coord()
#define COLOR        vec4(1.0)
#define LUTTexCoord  _glslp_tex_coord()
#define LUT_TexCoord _glslp_tex_coord()
)GLSL";

static const char* kFragmentHeader = R"GLSL(#version 450
#define PARAMETER_UNIFORM
#define FRAGMENT
#define COMPAT_TEXTURE   texture
#define COMPAT_PRECISION
#define FragColor _FragColor
layout(set = 2, binding = 0) uniform sampler2D Texture;
layout(set = 3, binding = 0) uniform LibretroUniforms {
    vec2  OutputSize;
    vec2  TextureSize;
    vec2  InputSize;
    uint  FrameCount;
    int   FrameDirection;
    vec2  OriginalSize;
    vec2  FinalViewportSize;
} u;
/* Same set convention as kVertexHeader, but fragment uniforms live at
 * set 3. The shared param_block_decl below references _GLSLP_PARAMS_SET. */
#define _GLSLP_PARAMS_SET 3
layout(location = 0) out vec4 _FragColor;
#define gl_FragColor _FragColor
#define OutputSize        u.OutputSize
#define TextureSize       u.TextureSize
#define InputSize         u.InputSize
#define FrameCount        int(u.FrameCount)
#define FrameDirection    u.FrameDirection
#define OriginalSize      u.OriginalSize
#define FinalViewportSize u.FinalViewportSize
)GLSL";

/* Strip the source's own `#define COMPAT_*` / `#define varying` /
 * `#define attribute` lines so OUR header-supplied macros win at
 * compile time. The source's defines target multiple GLSL versions
 * via __VERSION__ gates; we always emit GLSL 450 so a single
 * substitution per macro suffices.
 *
 * Also drop the source's own output declaration `out vec4 FragColor;`
 * (modern-GLSL branch in many libretro fragment shaders) — our
 * header declares `_FragColor` at location=0 and #defines FragColor
 * → _FragColor, so the source's declaration would be a double-decl. */
static void StripLibretroDefines(std::string& s) {
    std::string scrubbed;
    scrubbed.reserve(s.size());
    std::istringstream rs(s);
    std::string ln;
    while (std::getline(rs, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string tr = Trim(ln);
        if (tr.compare(0, 8, "#define ") == 0) {
            size_t p = 8;
            while (p < tr.size() && std::isspace((unsigned char)tr[p])) ++p;
            size_t name_start = p;
            while (p < tr.size() && !std::isspace((unsigned char)tr[p]) && tr[p] != '(') ++p;
            std::string name = tr.substr(name_start, p - name_start);
            if (name == "COMPAT_VARYING" || name == "COMPAT_ATTRIBUTE"
                || name == "COMPAT_TEXTURE" || name == "COMPAT_PRECISION"
                || name == "FragColor"
                /* Standard-uniform aliases the header already supplies.
                 * Sources like scanline.glsl re-#define these to wrap
                 * the value in a vec4(...), which collides with our
                 * `#define OutputSize u.OutputSize`. Drop the source-side
                 * version; the header's wins. */
                || name == "OutputSize" || name == "TextureSize"
                || name == "InputSize"  || name == "FrameCount"
                || name == "FrameDirection" || name == "OriginalSize"
                || name == "FinalViewportSize"
                || name == "MVPMatrix") {
                continue;  /* drop — header redefines */
            }
        }
        /* Drop a stray `#version N` line in the source body — our
         * stage header already emits `#version 450` and glslang
         * rejects a second `#version` directive that isn't the first
         * non-comment token. */
        if (tr.compare(0, 8, "#version") == 0
            && (tr.size() == 8 || std::isspace((unsigned char)tr[8]))) {
            continue;
        }
        /* Drop `out vec4 FragColor;` / `out vec3 FragColor;` etc. The
         * declaration appears in libretro shaders' modern-GLSL branch;
         * our header provides _FragColor at location=0. */
        if (tr.size() > 4 && tr.compare(0, 4, "out ") == 0
            && tr.find("FragColor") != std::string::npos) {
            continue;
        }
        scrubbed += ln; scrubbed += '\n';
    }
    s = std::move(scrubbed);
}

/* Rewrite ALL whole-word references to a parameter name as
 * `params.<name>` UNLESS the reference is preceded by a GLSL type or
 * storage/precision qualifier (in which case it's a declaration of a
 * local variable that happens to shadow the parameter).
 *
 * Imperfect: doesn't handle every shadowing case GLSL allows (e.g.
 * struct field names, function parameter names) but covers the
 * common one — local `float lum = ...;` inside a function where
 * `lum` is also a #pragma parameter. CRT-Geom hits exactly this case.
 *
 * The set of "declaration-context" tokens is the GLSL 450 type
 * keywords plus storage qualifiers and the libretro-specific
 * COMPAT_PRECISION marker that we strip elsewhere. */
static void RewriteParameterRefs(std::string& s,
                                 const std::vector<ShaderParam>& params) {
    if (params.empty()) return;
    static const std::set<std::string> kDeclContext = {
        "void", "float", "int", "uint", "bool", "double",
        "vec2", "vec3", "vec4", "dvec2", "dvec3", "dvec4",
        "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
        "bvec2", "bvec3", "bvec4",
        "mat2", "mat3", "mat4",
        "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4",
        "mat4x2", "mat4x3", "mat4x4",
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler2DRect", "sampler2DShadow", "samplerCubeShadow",
        "const", "in", "out", "inout", "uniform", "attribute", "varying",
        "highp", "mediump", "lowp", "COMPAT_PRECISION", "COMPAT_VARYING",
        "COMPAT_ATTRIBUTE",
    };
    auto is_id_char = [](char c) {
        return std::isalnum((unsigned char)c) != 0 || c == '_';
    };
    for (const auto& p : params) {
        const std::string& name = p.id;
        if (name.empty()) continue;
        std::string out_buf;
        out_buf.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            size_t pos = s.find(name, i);
            if (pos == std::string::npos) {
                out_buf.append(s, i, std::string::npos);
                break;
            }
            out_buf.append(s, i, pos - i);
            /* Word-boundary check. */
            const bool word_start = (pos == 0) || !is_id_char(s[pos - 1]);
            const size_t end = pos + name.size();
            const bool word_end = (end >= s.size()) || !is_id_char(s[end]);
            if (!word_start || !word_end) {
                out_buf.append(name);
                i = end;
                continue;
            }
            /* Preprocessor-line handling:
             *  - `#define NAME body`: the NAME slot is a declaration —
             *    keep it as-is. The body, however, is template code that
             *    later substitutes into non-preprocessor context, so any
             *    parameter refs inside it must be rewritten (else TEX2D's
             *    body `pow(texture(...), vec4(CRTgamma))` expands to a
             *    bare `CRTgamma` at the call site). Allow rewrite there.
             *  - `#if / #ifdef / #ifndef / #elif`: expressions reference
             *    PP macros, not GLSL identifiers. Skip the whole line.
             *  - `#pragma / #include / #version / #extension`: leave
             *    untouched.
             * Strategy: scan back to line start, peek at the directive.
             * For #define, also check whether `pos` falls inside the
             * NAME slot (the first identifier after `#define`) — if so,
             * skip; otherwise (we're in the body) allow rewrite. */
            size_t line_start = pos;
            while (line_start > 0 && s[line_start - 1] != '\n') --line_start;
            size_t ls = line_start;
            while (ls < s.size() && (s[ls] == ' ' || s[ls] == '\t')) ++ls;
            if (ls < s.size() && s[ls] == '#') {
                size_t hash = ls + 1;
                while (hash < s.size() && (s[hash] == ' ' || s[hash] == '\t')) ++hash;
                bool is_define = (hash + 6 < s.size()
                                  && s.compare(hash, 6, "define") == 0
                                  && (s[hash + 6] == ' ' || s[hash + 6] == '\t'));
                if (is_define) {
                    /* Locate the macro-name slot. */
                    size_t ns = hash + 6;
                    while (ns < s.size() && (s[ns] == ' ' || s[ns] == '\t')) ++ns;
                    size_t ne = ns;
                    while (ne < s.size() && is_id_char(s[ne])) ++ne;
                    if (pos >= ns && pos < ne) {
                        /* This occurrence IS the macro name — declaration. */
                        out_buf.append(name);
                        i = end;
                        continue;
                    }
                    /* Otherwise we're in the body — fall through to the
                     * normal previous-token check, so `float lum = ...`
                     * inside a multi-line macro body still works. */
                } else {
                    /* #if / #ifdef / #ifndef / #elif / #pragma / etc. */
                    out_buf.append(name);
                    i = end;
                    continue;
                }
            }
            /* Find the previous non-whitespace identifier token. */
            size_t back = pos;
            while (back > 0 && std::isspace((unsigned char)s[back - 1])) --back;
            size_t tok_end = back;
            while (back > 0 && is_id_char(s[back - 1])) --back;
            std::string prev_token = s.substr(back, tok_end - back);
            if (kDeclContext.count(prev_token)) {
                /* Local declaration shadowing the parameter — keep
                 * the variable name as-is. */
                out_buf.append(name);
            } else {
                out_buf.append("params.").append(name);
            }
            i = end;
        }
        s = std::move(out_buf);
    }
}

/* Rewrite `COMPAT_VARYING <type> <name>;` and `COMPAT_ATTRIBUTE <type>
 * <name>;` declarations with explicit `layout(location = N) <out|in>`
 * decorators. Each name gets a sequential location based on first
 * occurrence in the source; same name in vertex and fragment maps
 * to the same location so the linker matches them up.
 *
 * Caller passes the SAME varying_map across both stage rewrites so
 * locations stay synchronised. The map records the first-seen
 * location for each name; subsequent appearances reuse it (the
 * source may declare a varying in both `#if defined(VERTEX)` and
 * `#if defined(FRAGMENT)` branches with the same name). */
static void RewriteVaryingDecls(std::string& s,
                                std::map<std::string, int>& varying_map,
                                bool isVertex) {
    std::string scrubbed;
    scrubbed.reserve(s.size());
    std::istringstream rs(s);
    std::string ln;
    while (std::getline(rs, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string tr = Trim(ln);
        const std::string* keyword = nullptr;
        size_t kw_len = 0;
        if (tr.compare(0, 15, "COMPAT_VARYING ") == 0)        { static const std::string k = "COMPAT_VARYING";   keyword = &k; kw_len = 14; }
        else if (tr.compare(0, 17, "COMPAT_ATTRIBUTE ") == 0) { static const std::string k = "COMPAT_ATTRIBUTE"; keyword = &k; kw_len = 16; }
        else if (tr.compare(0,  8, "varying ") == 0)          { static const std::string k = "varying";          keyword = &k; kw_len = 7;  }
        else if (tr.compare(0, 10, "attribute ") == 0)        { static const std::string k = "attribute";        keyword = &k; kw_len = 9;  }

        if (keyword) {
            /* Pull <type> <name>. After the keyword + space, walk
             * forward to find the semicolon and split. */
            std::string rest = Trim(tr.substr(kw_len));
            size_t sc = rest.find(';');
            std::string body = (sc == std::string::npos) ? rest : rest.substr(0, sc);
            /* Last whitespace-separated token of `body` is the
             * variable name. */
            size_t sp = body.find_last_of(" \t");
            if (sp != std::string::npos) {
                std::string name = body.substr(sp + 1);
                const bool is_input = !isVertex || (*keyword == "COMPAT_ATTRIBUTE")
                                                 || (*keyword == "attribute");
                /* Vertex-stage attribute inputs: libretro convention is
                 * to read position/UV from `VertexCoord` / `TexCoord` /
                 * `COLOR` / `LUT_TexCoord` (and possibly numbered
                 * variants). We don't bind a vertex buffer — the runtime
                 * draws a 4-vertex strip with gl_VertexIndex only — so
                 * these inputs would be all-zero. Instead, the vertex
                 * header defines them as macros that return values
                 * derived from gl_VertexIndex. Strip the declaration
                 * here so it doesn't conflict. */
                if (isVertex && is_input) {
                    static const char* kSynthesised[] = {
                        "VertexCoord", "TexCoord", "COLOR",
                        "LUTTexCoord", "LUT_TexCoord",
                    };
                    bool synth = false;
                    for (const char* n : kSynthesised) {
                        if (name == n) { synth = true; break; }
                    }
                    if (synth) continue;  /* drop the declaration */
                }
                int loc;
                auto it = varying_map.find(name);
                if (it == varying_map.end()) {
                    loc = (int)varying_map.size();
                    varying_map.emplace(name, loc);
                } else {
                    loc = it->second;
                }
                char prefix[64];
                std::snprintf(prefix, sizeof(prefix),
                              "layout(location = %d) %s ", loc,
                              is_input ? "in" : "out");
                scrubbed += prefix;
                scrubbed += body;
                scrubbed += ";\n";
                continue;
            }
        }
        scrubbed += ln; scrubbed += '\n';
    }
    s = std::move(scrubbed);
}

/* Split the source into the two stage bodies. libretro uses
 * `#pragma stage vertex` / `#pragma stage fragment` markers; lines
 * outside any block are considered shared (preprocessor directives,
 * uniforms, shared helper functions). The result concatenates each
 * stage's block-specific lines with the shared lines preserved in
 * both. */
static void SplitStages(const std::string& source,
                        std::string& shared_out,
                        std::string& vertex_out,
                        std::string& fragment_out) {
    enum Mode { Shared, Vertex, Fragment };
    Mode mode = Shared;
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line)) {
        /* Strip trailing CR if file is CRLF. */
        if (!line.empty() && line.back() == '\r') line.pop_back();

        /* Detect stage-switch pragmas. Match leading whitespace +
         * `#pragma stage <name>`. */
        size_t hash = line.find_first_not_of(" \t");
        if (hash != std::string::npos && line.compare(hash, 14, "#pragma stage ") == 0) {
            std::string name = line.substr(hash + 14);
            /* Trim. */
            size_t lo = name.find_first_not_of(" \t");
            size_t hi = name.find_last_not_of(" \t");
            if (lo != std::string::npos) name = name.substr(lo, hi - lo + 1);
            if      (name == "vertex")   mode = Vertex;
            else if (name == "fragment") mode = Fragment;
            else                          mode = Shared;
            /* Pragma line itself is dropped from the output. */
            continue;
        }

        switch (mode) {
            case Shared:   shared_out   += line; shared_out   += '\n'; break;
            case Vertex:   vertex_out   += line; vertex_out   += '\n'; break;
            case Fragment: fragment_out += line; fragment_out += '\n'; break;
        }
    }
}

}  // namespace

std::optional<PreprocessedShader> PreprocessLibretroGlsl(const std::string& source) {
    PreprocessedShader out;

    /* Extract `#pragma parameter` lines first — they're metadata, not
     * shader code. Drop them from the body. */
    std::string filtered;
    filtered.reserve(source.size());
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t hash = line.find_first_not_of(" \t");
        if (hash != std::string::npos
            && line.compare(hash, 18, "#pragma parameter ") == 0) {
            if (auto p = ParsePragmaParameter(line.substr(hash + 18))) {
                out.parameters.push_back(std::move(*p));
            }
            continue;  /* Drop the pragma from the output stream. */
        }
        filtered += line;
        filtered += '\n';
    }

    /* (Old global ReplaceAll of precision qualifiers removed — it
     * mangled the source's own `#define COMPAT_PRECISION mediump`
     * lines into `#define  ` which glslang rejects. The strip pass
     * below now skips leading precision qualifiers locally per line
     * via PrecisionAwareUniformLine().) */

    /* Vulkan SPIR-V doesn't allow non-opaque uniforms outside a block,
     * but libretro shaders declare each `#pragma parameter` as a bare
     * `uniform float NAME;`. Strip those declarations and add a
     * uniform block at the top of each stage with the parameter
     * fields. Non-parameter `uniform <type> NAME;` lines are also
     * stripped with a stderr warning so shaders with custom uniforms
     * don't link cleanly until a future stage adds them properly. */
    std::string param_block_decl;
    if (!out.parameters.empty()) {
        param_block_decl = "layout(set = _GLSLP_PARAMS_SET, binding = 1) uniform ShaderParams {\n";
        for (const auto& p : out.parameters) {
            param_block_decl += "    float " + p.id + ";\n";
        }
        param_block_decl += "} params;\n";
        /* No `#define NAME params.NAME` aliases — those are textual
         * substitutions and trip on local variables that happen to
         * shadow a parameter (CRT-Geom uses `lum` as both a
         * #pragma parameter and a local variable in `saturation()`).
         * Instead we walk the source below and rewrite only
         * identifier REFERENCES (not declarations) to params.NAME. */
    }
    /* Strip non-opaque `uniform <type> NAME;` lines, and decorate
     * `uniform sampler2D NAME;` declarations with explicit
     * `layout(set = 2, binding = K)` so Vulkan SPIR-V is happy.
     * Binding 0 stays reserved for the primary pass input named
     * "Texture"; LUT samplers (any other sampler) get sequential
     * bindings 1, 2, 3, ... in source-declaration order. The runtime
     * uses out.lut_sampler_names to wire LUTs at the matching slots. */
    auto strip_leading_qualifiers = [](const std::string& s) {
        std::string r = s;
        auto strip_prefix = [&](const std::string& prefix) {
            while (true) {
                size_t lo = r.find_first_not_of(" \t");
                if (lo == std::string::npos) return;
                if (r.compare(lo, prefix.size(), prefix) != 0) return;
                /* must be followed by whitespace, not e.g. an identifier */
                if (lo + prefix.size() < r.size()
                    && !std::isspace((unsigned char)r[lo + prefix.size()])) return;
                r.erase(lo, prefix.size());
            }
        };
        strip_prefix("COMPAT_PRECISION");
        strip_prefix("mediump");
        strip_prefix("highp");
        strip_prefix("lowp");
        return r;
    };
    {
        std::string scrubbed;
        scrubbed.reserve(filtered.size());
        std::istringstream rs(filtered);
        std::string ln;
        int next_lut_binding = 1;
        while (std::getline(rs, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            std::string tr = Trim(strip_leading_qualifiers(ln));
            if (tr.size() > 8 && tr.compare(0, 8, "uniform ") == 0) {
                /* Sampler / image uniform: opaque, allowed bare in
                 * Vulkan but still needs a layout decoration. Detect
                 * `sampler` substring; everything else falls through
                 * to the strip path below. */
                if (tr.find("sampler") != std::string::npos
                    || tr.find("image")   != std::string::npos) {
                    /* Pull the sampler name (last token before `;`). */
                    size_t sc = tr.find(';');
                    std::string body = (sc == std::string::npos)
                                       ? tr.substr(8)
                                       : tr.substr(8, sc - 8);
                    size_t sp = body.find_last_of(" \t");
                    std::string name = (sp == std::string::npos) ? body
                                                                  : body.substr(sp + 1);
                    if (name == "Texture") {
                        /* The primary input is already declared in
                         * the fragment header at set=2/binding=0;
                         * drop the source's redundant copy to avoid
                         * a double-declaration compile error. */
                        continue;
                    }
                    int binding = next_lut_binding++;
                    out.lut_sampler_names.push_back(name);
                    /* Rewrite with the layout decorator. */
                    char prefix[64];
                    std::snprintf(prefix, sizeof(prefix),
                                  "layout(set = 2, binding = %d) ", binding);
                    /* Preserve original spacing of the rest of the line. */
                    scrubbed += prefix;
                    scrubbed += ln;
                    scrubbed += '\n';
                    continue;
                }
                /* Non-opaque uniform: drop. We supply the standard
                 * libretro uniforms via the LibretroUniforms block in
                 * the header; per-#pragma-parameter uniforms via the
                 * ShaderParams block. Source declarations of any of
                 * those are silently dropped because they conflict
                 * with our definitions. Anything truly unknown gets
                 * a stderr warning. */
                static const char* kStandardLibretroUniforms[] = {
                    "MVPMatrix", "OutputSize", "TextureSize", "InputSize",
                    "FrameCount", "FrameDirection", "OriginalSize",
                    "FinalViewportSize",
                };
                size_t sc = tr.find(';');
                if (sc != std::string::npos) {
                    std::string body = tr.substr(8, sc - 8);
                    /* Split into whitespace-separated tokens. body looks
                     * like `float foo` / `vec2 BLOOM_APPROXtexture_size`
                     * / `COMPAT_PRECISION float foo` / `lowp vec2 X`.
                     * The last token is the name; the previous non-
                     * precision-qualifier token is the type. */
                    std::vector<std::string> toks;
                    {
                        std::istringstream bs(body);
                        std::string t;
                        while (bs >> t) toks.push_back(std::move(t));
                    }
                    if (toks.empty()) { continue; }
                    std::string name = toks.back();
                    std::string type;
                    for (auto it = toks.rbegin() + 1; it != toks.rend(); ++it) {
                        if (*it == "COMPAT_PRECISION" || *it == "highp"
                            || *it == "mediump" || *it == "lowp"
                            || *it == "const") continue;
                        type = *it;
                        break;
                    }
                    bool standard = false;
                    for (const char* sn : kStandardLibretroUniforms) {
                        if (name == sn) { standard = true; break; }
                    }
                    if (standard) {
                        continue;  /* silent drop — we supplied it via the header */
                    }
                    bool known = false;
                    for (const auto& p : out.parameters) {
                        if (p.id == name) { known = true; break; }
                    }
                    if (!known) {
                        /* Auto-stub unknown uniforms with type-appropriate
                         * defaults so shaders that rely on libretro's
                         * runtime-uniform override mechanism (crt-royale,
                         * etc.) still compile. Values are zero (or 1 for
                         * sizes that get divided into) — won't match the
                         * intended look, but a stable pipeline is better
                         * than failing to compile. Eventually replace
                         * with proper preset-driven overrides. */
                        std::string default_expr;
                        if      (type == "float")  default_expr = "0.0";
                        else if (type == "int")    default_expr = "0";
                        else if (type == "uint")   default_expr = "0u";
                        else if (type == "bool")   default_expr = "false";
                        else if (type == "vec2") {
                            /* Size-style uniforms (ending in *size,
                             * *Size, *_size) are usually divided into,
                             * so default to non-zero. */
                            if (name.find("size") != std::string::npos
                                || name.find("Size") != std::string::npos)
                                default_expr = "vec2(1.0, 1.0)";
                            else
                                default_expr = "vec2(0.0)";
                        }
                        else if (type == "vec3")   default_expr = "vec3(0.0)";
                        else if (type == "vec4")   default_expr = "vec4(0.0)";
                        else if (type == "ivec2")  default_expr = "ivec2(0)";
                        else if (type == "ivec3")  default_expr = "ivec3(0)";
                        else if (type == "ivec4")  default_expr = "ivec4(0)";
                        else if (type == "mat2")   default_expr = "mat2(1.0)";
                        else if (type == "mat3")   default_expr = "mat3(1.0)";
                        else if (type == "mat4")   default_expr = "mat4(1.0)";
                        if (!default_expr.empty()) {
                            scrubbed += "#define " + name + " " + default_expr + "\n";
                            continue;  /* don't emit the uniform decl */
                        }
                        /* Type we don't know how to default — drop with a warning. */
                        std::fprintf(stderr,
                            "[glslp] preprocessor: dropped uniform '%s' (unknown type '%s'); "
                            "shader may have undefined references\n",
                            name.c_str(), type.c_str());
                    }
                }
                continue;
            }
            scrubbed += ln; scrubbed += '\n';
        }
        filtered = std::move(scrubbed);
    }

    /* Split into shared / vertex-only / fragment-only blocks. */
    std::string shared, vertex_body, fragment_body;
    SplitStages(filtered, shared, vertex_body, fragment_body);

    /* If no stage markers were found, two sub-cases:
     *   - Real libretro shaders use `#if defined(VERTEX)` /
     *     `#elif defined(FRAGMENT)` to gate per-stage code in the
     *     same file. We send the entire source to BOTH vertex_body
     *     and fragment_body; the stage headers `#define VERTEX`
     *     vs `#define FRAGMENT` make each compile pick its branch.
     *   - Simple post-process shaders (the synthetic invert demo)
     *     have no per-stage code at all; the same dual-emission works
     *     for them too — the fragment body's gl_FragColor write is
     *     skipped during vertex compile because no `#if defined(VERTEX)`
     *     branch exists, and the missing vertex main() is supplied
     *     by a synthesised passthrough we prepend below. */
    if (vertex_body.empty() && fragment_body.empty()) {
        const std::string passthrough_vertex =
            "#ifndef VERTEX_MAIN_PROVIDED\n"
            "/* Synthesised fullscreen-quad vertex shader for sources\n"
            " * that don't gate a vertex stage of their own. Picked up\n"
            " * when the source has no `#if defined(VERTEX)` block\n"
            " * AND no #pragma stage vertex section. */\n"
            "layout(location = 0) out vec2 vTexCoord;\n"
            "void main() {\n"
            "    vec2 pos = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,\n"
            "                    (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);\n"
            "    vTexCoord = vec2((gl_VertexIndex & 1) == 0 ?  0.0 : 1.0,\n"
            "                     (gl_VertexIndex & 2) == 0 ?  1.0 : 0.0);\n"
            "    gl_Position = vec4(pos, 0.0, 1.0);\n"
            "}\n"
            "#endif\n";
        /* Heuristic: if the shared body contains `defined(VERTEX)` or
         * `defined(FRAGMENT)`, the source provides its own per-stage
         * main()s — don't synthesise a vertex one. Use a `#define
         * VERTEX_MAIN_PROVIDED` to suppress the passthrough. */
        const bool source_has_vertex_main =
            shared.find("defined(VERTEX)")   != std::string::npos ||
            shared.find("#ifdef VERTEX")      != std::string::npos;
        std::string vertex_pre;
        if (source_has_vertex_main) {
            vertex_pre = "#define VERTEX_MAIN_PROVIDED\n";
        }
        vertex_body   = vertex_pre + shared + passthrough_vertex;
        fragment_body = shared;
        shared.clear();
        /* Fragment-side stub: synthesised passthroughs (no
         * `#if defined(FRAGMENT)` in source) need a fragment main()
         * too. Emit one when the source doesn't provide its own. */
        const bool source_has_fragment_main =
            fragment_body.find("defined(FRAGMENT)")   != std::string::npos ||
            fragment_body.find("#ifdef FRAGMENT")      != std::string::npos;
        if (!source_has_fragment_main && fragment_body.find("void main()") == std::string::npos) {
            fragment_body = "layout(location = 0) in vec2 vTexCoord;\n" + fragment_body;
        }
    }

    /* Strip the source's own #define COMPAT_* lines (and any stray
     * `out vec4 FragColor;` decl) BEFORE concatenating, so the strip
     * doesn't accidentally remove our header-supplied versions. */
    StripLibretroDefines(shared);
    StripLibretroDefines(vertex_body);
    StripLibretroDefines(fragment_body);

    /* Build full stage modules: header + parameter block + shared +
     * stage-specific. Parameter block is empty when no parameters
     * exist; safe to concatenate. */
    out.vertex_glsl   = std::string(kVertexHeader)   + param_block_decl + shared + vertex_body;
    out.fragment_glsl = std::string(kFragmentHeader) + param_block_decl + shared + fragment_body;

    /* Rewrite COMPAT_VARYING / COMPAT_ATTRIBUTE / varying / attribute
     * declarations with sequential layout(location = N) decorators.
     * Use a SHARED map so each name gets the same location in both
     * stages — required for the linker to match vertex outputs to
     * fragment inputs. */
    std::map<std::string, int> varying_map;
    RewriteVaryingDecls(out.vertex_glsl,   varying_map, /*isVertex=*/true);
    RewriteVaryingDecls(out.fragment_glsl, varying_map, /*isVertex=*/false);

    /* Replace each parameter NAME (whole-word, non-declaration
     * contexts only) with `params.NAME`. Handles the CRT-Geom case
     * where a #pragma parameter has the same name as a local
     * variable inside a function — the local declaration keeps the
     * short name, references resolve via params.NAME. */
    RewriteParameterRefs(out.vertex_glsl,   out.parameters);
    RewriteParameterRefs(out.fragment_glsl, out.parameters);

    return out;
}

/* ----- Step 6: LUT image loader ------------------------------------- */

namespace {

/* Read N bytes from `path` into a buffer; small wrapper over
 * ReadFileBytes that returns the vector directly. */
static std::optional<std::vector<uint8_t>> ReadFileVec(const std::string& path) {
    std::vector<uint8_t> v;
    if (!ReadFileBytes(path, v)) return std::nullopt;
    return v;
}

/* Detect file type by magic. PNG starts with 0x89 'P' 'N' 'G'. TGA
 * has no robust magic; fall back to filename extension. */
enum class LutImageFormat { Png, Tga, Unknown };
static LutImageFormat DetectLutFormat(const std::string& path,
                                      const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        return LutImageFormat::Png;
    }
    const std::string lower = [&] {
        std::string s = path;
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }();
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tga") {
        return LutImageFormat::Tga;
    }
    return LutImageFormat::Unknown;
}

/* TGA decoder — uncompressed truecolor (24/32-bit) only. Image
 * descriptor's bit 5 controls origin (0 = bottom-left, 1 = top-left);
 * we flip if needed so the output is always top-left. Format covers
 * the LUTs in libretro/glsl-shaders that aren't PNG. */
static std::optional<DecodedImage> DecodeTga(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 18) return std::nullopt;
    /* Header layout (little-endian): [0] id_len, [1] cmap_type,
     * [2] image_type, [3..7] cmap spec, [8..11] x/y origin,
     * [12..13] width, [14..15] height, [16] pixel_depth,
     * [17] image_descriptor. */
    if (bytes[1] != 0)        return std::nullopt;  /* no colormaps */
    if (bytes[2] != 2)        return std::nullopt;  /* uncompressed truecolor only */
    uint16_t w = (uint16_t)bytes[12] | ((uint16_t)bytes[13] << 8);
    uint16_t h = (uint16_t)bytes[14] | ((uint16_t)bytes[15] << 8);
    uint8_t  depth = bytes[16];
    uint8_t  desc  = bytes[17];
    if (w == 0 || h == 0)         return std::nullopt;
    if (depth != 24 && depth != 32) return std::nullopt;
    const size_t bpp = depth / 8;
    const size_t need = 18 + (size_t)bytes[0] + (size_t)w * h * bpp;
    if (bytes.size() < need) return std::nullopt;
    const uint8_t* px = bytes.data() + 18 + bytes[0];
    DecodedImage img;
    img.width  = w;
    img.height = h;
    img.rgba.resize((size_t)w * h * 4);
    const bool topLeft = (desc & 0x20) != 0;
    for (int y = 0; y < h; ++y) {
        const int src_y = topLeft ? y : (h - 1 - y);
        const uint8_t* row = px + (size_t)src_y * w * bpp;
        uint8_t* out = img.rgba.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = row + x * bpp;
            /* TGA stores BGR(A); swap to RGBA. */
            out[0] = p[2];
            out[1] = p[1];
            out[2] = p[0];
            out[3] = (bpp == 4) ? p[3] : 0xFF;
            out += 4;
        }
    }
    return img;
}

}  // namespace

/* PNG decoder via libpng. libpng is already linked into the engine
 * (port_bugreport.cpp uses it for screenshot encoding); we reuse the
 * same set of symbols here. RGB inputs get an alpha channel filled
 * with 0xFF to produce an ABGR8888-compatible buffer.
 *
 * Implementation detail: libpng's reading API is callback-based.
 * Easier to push the file bytes in from memory than to drive its
 * file-pointer interface. The png_set_read_fn callback below
 * advances through the in-memory buffer that ReadPng captured. */
static std::optional<DecodedImage> DecodePng(const std::vector<uint8_t>& bytes);

std::optional<DecodedImage> LoadLutImage(const std::string& path) {
    auto bytes_opt = ReadFileVec(path);
    if (!bytes_opt) {
        std::fprintf(stderr, "[glslp] LUT open failed: %s\n", path.c_str());
        return std::nullopt;
    }
    const auto& bytes = *bytes_opt;
    switch (DetectLutFormat(path, bytes)) {
        case LutImageFormat::Png: return DecodePng(bytes);
        case LutImageFormat::Tga: return DecodeTga(bytes);
        case LutImageFormat::Unknown:
            std::fprintf(stderr, "[glslp] LUT format not recognised: %s\n", path.c_str());
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace PortGlslp

/* PNG decoder implementation. Pulled out of the PortGlslp namespace so
 * it can be defined after the libpng include without polluting the
 * header surface. */
#include <png.h>

namespace PortGlslp {

namespace {
struct PngMemReader {
    const uint8_t* data;
    size_t         remaining;
};
static void PngReadFromMemory(png_structp png_ptr, png_bytep target, png_size_t n) {
    PngMemReader* r = static_cast<PngMemReader*>(png_get_io_ptr(png_ptr));
    if (r->remaining < n) { png_error(png_ptr, "short read"); return; }
    std::memcpy(target, r->data, n);
    r->data      += n;
    r->remaining -= n;
}
}  // namespace

static std::optional<DecodedImage> DecodePng(const std::vector<uint8_t>& bytes) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return std::nullopt;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return std::nullopt;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return std::nullopt;
    }

    PngMemReader reader{ bytes.data(), bytes.size() };
    png_set_read_fn(png, &reader, PngReadFromMemory);

    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    int bit_depth  = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);
    /* Reject implausible dimensions before allocating: a crafted PNG header
     * could request a multi-gigabyte (w*h*4) buffer -> bad_alloc/terminate.
     * Real shader LUTs are tiny; 8192 per side is already far beyond any. */
    if (w == 0 || h == 0 || w > 8192 || h > 8192) {
        std::fprintf(stderr, "[glslp] LUT rejected: implausible dimensions %ux%u\n",
                     (unsigned)w, (unsigned)h);
        png_destroy_read_struct(&png, &info, nullptr);
        return std::nullopt;
    }

    /* Normalise to 8-bit RGBA. The libretro LUTs we care about are
     * 24-bit truecolor (no alpha) or 32-bit RGBA. */
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    DecodedImage img;
    img.width  = (int)w;
    img.height = (int)h;
    img.rgba.resize((size_t)w * h * 4);
    std::vector<png_bytep> rows(h);
    for (png_uint_32 y = 0; y < h; ++y) {
        rows[y] = img.rgba.data() + (size_t)y * w * 4;
    }
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    return img;
}

}  // namespace PortGlslp

/* C-callable runtime API lives in port_glslp_runtime.cpp now (Step 5).
 * Stubs removed from this file to avoid duplicate definitions. */
