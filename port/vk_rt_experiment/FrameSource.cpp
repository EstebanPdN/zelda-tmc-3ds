/* port/vk_rt_experiment/FrameSource.cpp
 *
 * libpng-backed PNG decoder + advance() ring. Decoded frames are
 * stored as flat uint8_t[w*h*4] RGBA buffers; alpha is 255 for the
 * RGB-source screenshots we expect (the GBA framebuffer is opaque).
 */

#include "FrameSource.h"

#include <png.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tmc_vkrt {

bool FrameSource::loadDirectory(const std::string& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::fprintf(stderr, "[fsrc] '%s' is not a directory\n", dir.c_str());
        return false;
    }

    /* Collect candidate filenames first so we can sort them — PNG
     * load order would otherwise depend on filesystem iteration. */
    std::vector<std::string> paths;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto& p = entry.path();
        if (p.extension() != ".png") continue;
        const std::string name = p.filename().string();
        if (name.rfind("tmc-", 0) != 0) continue;  /* only tmc-*.png */
        paths.push_back(p.string());
    }
    /* Numeric-aware sort so tmc-2.png comes before tmc-10.png. */
    std::sort(paths.begin(), paths.end(),
              [](const std::string& a, const std::string& b) {
                  /* Compare by length first (shorter = smaller number
                   * when the prefix is identical), then lexicographic
                   * within same length. */
                  if (a.size() != b.size()) return a.size() < b.size();
                  return a < b;
              });

    mFrames.clear();
    for (const std::string& path : paths) {
        auto rgba = decodePng(path);
        if (!rgba.empty()) {
            mFrames.push_back(std::move(rgba));
        }
    }
    std::fprintf(stderr, "[fsrc] loaded %zu/%zu PNG frame(s) from %s\n",
                 mFrames.size(), paths.size(), dir.c_str());
    return !mFrames.empty();
}

std::vector<uint8_t> FrameSource::decodePng(const std::string& path) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return {};

    /* libpng boilerplate. The struct + io setup is identical across
     * every project; comments stay light. */
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); return {}; }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return {};
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return {};
    }
    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    png_byte    colorType = png_get_color_type(png, info);
    png_byte    bitDepth  = png_get_bit_depth(png, info);

    /* Normalise to 8-bit RGBA via libpng's transform pipeline. The
     * screenshots are 8-bit RGB; tRNS / palette / 16-bit paths get
     * handled too in case anything's weird. */
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    if ((int)w != kFrameW || (int)h != kFrameH) {
        std::fprintf(stderr,
            "[fsrc] %s is %ux%u, expected %dx%d — skipping\n",
            path.c_str(), w, h, kFrameW, kFrameH);
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return {};
    }

    std::vector<uint8_t> out(kFrameBytes);
    /* png_read_image takes one row pointer per row, top-to-bottom. */
    std::vector<png_bytep> rows(kFrameH);
    for (int y = 0; y < kFrameH; ++y) {
        rows[y] = out.data() + (size_t)y * kFrameW * 4;
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);
    return out;
}

const uint8_t* FrameSource::advance(int framesPerStep) {
    if (mFrames.empty()) return nullptr;
    if (framesPerStep < 1) framesPerStep = 1;
    if (++mCallsSinceStep >= framesPerStep) {
        mCallsSinceStep = 0;
        mCurrent = (mCurrent + 1) % mFrames.size();
    }
    return mFrames[mCurrent].data();
}

/* ---------- ShmFrameSource ---------- */

static constexpr uint32_t kShmMagic   = 0x46434D54u;  /* "TMCF" little-endian */
static constexpr uint32_t kShmVersion = 1u;
static constexpr size_t   kShmHeader  = 24;

bool ShmFrameSource::open(const char* shmName) {
    if (mBase) return true;  /* already open */

    mFd = shm_open(shmName, O_RDONLY, 0);
    if (mFd < 0) {
        std::fprintf(stderr, "[shm-rd] shm_open(%s) failed (is tmc_pc running with TMC_PUBLISH_FRAMEBUFFER=1?)\n",
                     shmName);
        return false;
    }

    struct stat st{};
    if (fstat(mFd, &st) < 0 || (size_t)st.st_size < kShmHeader) {
        std::fprintf(stderr, "[shm-rd] fstat / size too small\n");
        ::close(mFd);
        mFd = -1;
        return false;
    }
    mBytes = (size_t)st.st_size;

    /* Map read-only — we never write to the producer's region. */
    void* p = mmap(nullptr, mBytes, PROT_READ, MAP_SHARED, mFd, 0);
    if (p == MAP_FAILED) {
        std::fprintf(stderr, "[shm-rd] mmap failed\n");
        ::close(mFd);
        mFd = -1;
        return false;
    }
    mBase = p;

    /* Sanity-check the header before trusting the rest. Accept any
     * known version (1..4): v1 was framebuffer-only, v2 added the
     * OAM block, v3 added per-BG-layer planes, v4 added the OAM-only
     * sprite plane. */
    const uint32_t* h = (const uint32_t*)mBase;
    if (h[0] != kShmMagic || h[1] < 1 || h[1] > 4) {
        std::fprintf(stderr, "[shm-rd] bad header magic/version (got %08x/%u, want %08x/[1..4])\n",
                     h[0], h[1], kShmMagic);
        munmap(mBase, mBytes);
        ::close(mFd);
        mBase = nullptr; mFd = -1;
        return false;
    }
    mWidth   = (int)h[2];
    mHeight  = (int)h[3];
    mVersion = h[1];
    std::fprintf(stderr, "[shm-rd] opened %s — %dx%d, v%u, frame=%u\n",
                 shmName, mWidth, mHeight, mVersion, h[4]);
    return true;
}

void ShmFrameSource::close() {
    if (mBase) {
        munmap(mBase, mBytes);
        mBase = nullptr;
    }
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    mBytes = 0;
    mWidth = mHeight = 0;
}

const uint8_t* ShmFrameSource::currentFrame(uint32_t* outFrameSeq) const {
    if (!mBase) return nullptr;
    const uint32_t* h = (const uint32_t*)mBase;
    if (outFrameSeq) *outFrameSeq = __atomic_load_n(&h[4], __ATOMIC_ACQUIRE);
    return (const uint8_t*)mBase + kShmHeader;
}

const uint16_t* ShmFrameSource::currentOam() const {
    if (!mBase || mVersion < 2) return nullptr;
    const size_t pixelsBytes = (size_t)mWidth * mHeight * 4;
    /* OAM block starts immediately after the pixel block. */
    return reinterpret_cast<const uint16_t*>(
        (const uint8_t*)mBase + kShmHeader + pixelsBytes);
}

const uint8_t* ShmFrameSource::currentBgPlane(int planeIndex) const {
    if (!mBase || mVersion < 3 || planeIndex < 0 || planeIndex >= 2) return nullptr;
    const size_t pixelsBytes = (size_t)mWidth * mHeight * 4;
    const size_t kOamBytes = 1024;
    /* BG planes start after the composite framebuffer + OAM block. */
    const size_t off = kShmHeader + pixelsBytes + kOamBytes
                       + (size_t)planeIndex * pixelsBytes;
    return (const uint8_t*)mBase + off;
}

const uint8_t* ShmFrameSource::currentSpritePlane() const {
    if (!mBase || mVersion < 4) return nullptr;
    const size_t pixelsBytes = (size_t)mWidth * mHeight * 4;
    const size_t kOamBytes = 1024;
    /* Sprite plane (OAM-only render, alpha=0 outside silhouettes) sits
     * immediately after the 2 BG planes — plane index 2 in the layout. */
    const size_t off = kShmHeader + pixelsBytes + kOamBytes
                       + (size_t)2 * pixelsBytes;
    return (const uint8_t*)mBase + off;
}

/* ---------- ParsedOam ---------- */

/* GBA OAM layout (per https://problemkaputt.de/gbatek.htm):
 *   attr0  bit 0..7   y coordinate
 *          bit 8..9   rotation/scaling flag
 *                       00: normal, 01: affine, 10: hidden, 11: affine 2x
 *          bit 10..11 mode (0=normal, 1=semi-transparent, 2=window, 3=prohibited)
 *          bit 12     mosaic
 *          bit 13     8bpp vs 4bpp
 *          bit 14..15 shape (0=square, 1=horizontal, 2=vertical)
 *   attr1  bit 0..8   x coordinate (sign-extended from 9 bits)
 *          bit 9..13  affine index (when bit 9 of attr0 set)
 *                     OR bits 12,13 = h/v flip (when not affine)
 *          bit 14..15 size (combined with shape → 12 valid combos)
 *   attr2  bit 0..9   tile index
 *          bit 10..11 priority (0=front, 3=back)
 *          bit 12..15 palette bank (4bpp only)
 *
 * Shape×size → dimensions (in pixels):
 *   shape 0 square:    [8,16,32,64] both
 *   shape 1 horizontal:[16x8, 32x8, 32x16, 64x32]
 *   shape 2 vertical:  [8x16, 8x32, 16x32, 32x64]
 */
static const int kSpriteDims[3][4][2] = {
    {{8, 8},  {16,16}, {32,32}, {64,64}},  /* shape 0 (square) */
    {{16, 8}, {32, 8}, {32,16}, {64,32}},  /* shape 1 (horizontal) */
    {{ 8,16}, { 8,32}, {16,32}, {32,64}},  /* shape 2 (vertical) */
};

void ParsedOam::parse(const uint16_t* oam, int count) {
    mSprites.clear();
    if (!oam) return;
    for (int i = 0; i < count; ++i) {
        const uint16_t attr0 = oam[i * 4 + 0];
        const uint16_t attr1 = oam[i * 4 + 1];
        const uint16_t attr2 = oam[i * 4 + 2];

        const int  rotScale  = (attr0 >> 8) & 0x3;
        const bool hidden    = (rotScale == 2);   /* hidden, non-affine */
        if (hidden) continue;

        const int shape = (attr0 >> 14) & 0x3;
        const int size  = (attr1 >> 14) & 0x3;
        if (shape > 2) continue;  /* shape=3 is prohibited */

        const int w = kSpriteDims[shape][size][0];
        const int h = kSpriteDims[shape][size][1];

        int y = attr0 & 0xFF;       /* 8-bit unsigned 0..255 */
        if (y >= 160) y -= 256;     /* GBA wraps high y to negative */
        int x = attr1 & 0x1FF;      /* 9-bit unsigned */
        if (x >= 240) x -= 512;     /* wraps high x to negative */

        /* Cull fully off-screen. */
        if (x + w <= 0 || y + h <= 0 || x >= 240 || y >= 160) continue;

        OamSprite s{};
        s.x            = x;
        s.y            = y;
        s.w            = w;
        s.h            = h;
        s.paletteIndex = (uint8_t)((attr2 >> 12) & 0xF);
        s.priority     = (uint8_t)((attr2 >> 10) & 0x3);
        s.oamIndex     = i;
        mSprites.push_back(s);
    }
}

}  /* namespace tmc_vkrt */
