/* port/vk_rt_experiment/FrameSource.h
 *
 * Stand-in for the live GBA framebuffer (`virtuappu_frame_buffer` in
 * tmc_pc). Loads a directory of pre-captured 240×160 RGBA PNG
 * screenshots at startup, then `currentFrame()` returns the bytes of
 * one frame per call — advancing through the loaded set at a tunable
 * rate so the RT pipeline sees a changing-each-frame texture, the
 * same shape it would see if hooked to the real PPU.
 *
 * Why screenshots and not a live IPC connection? Two reasons:
 *   1. The RT scaffold is a separate binary from tmc_pc — linking
 *      them at this stage means changing the main game's build,
 *      which is bigger than the "prove the bridge" goal.
 *   2. The captured PNGs let us iterate on the RT side without
 *      having to run a full TMC session each time.
 *
 * The actual GBA-frame-to-RT path is identical: `uint8_t*` of size
 * 240*160*4, uploaded via staging buffer + image-copy command. Once
 * this is proven, swapping screenshots for a shared-memory feed from
 * tmc_pc is a few-dozen-line change.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tmc_vkrt {

class FrameSource {
public:
    /* Width/height of every frame in the source set. Matches the
     * GBA's native framebuffer dimensions exactly so the upload sizing
     * doesn't depend on the source. */
    static constexpr int kFrameW = 240;
    static constexpr int kFrameH = 160;
    static constexpr size_t kFrameBytes =
        static_cast<size_t>(kFrameW) * kFrameH * 4;

    /* Load every `tmc-*.png` in `dir` whose dimensions are kFrameW ×
     * kFrameH. Returns true if at least one was loaded. Frames are
     * stored in lexicographic name order — so tmc-0.png is at index
     * 0, tmc-1.png at 1, ... tmc-17.png at 17. */
    bool loadDirectory(const std::string& dir);

    /* Number of distinct frames loaded. 0 if loadDirectory failed. */
    size_t frameCount() const { return mFrames.size(); }

    /* Advance the internal frame index and return the new current
     * frame's RGBA bytes. `framesPerStep` lets the caller throttle:
     * e.g. RT runs at 60fps but we only advance the PPU frame every
     * 10 calls (= ~6fps source rate). Returns nullptr if no frames
     * were loaded. The pointer is valid until the next call. */
    const uint8_t* advance(int framesPerStep);

    /* Read-only access to the current frame without advancing. */
    const uint8_t* currentFrame() const {
        if (mFrames.empty()) return nullptr;
        return mFrames[mCurrent].data();
    }

private:
    /* Decode one PNG file from `path` into a kFrameW × kFrameH RGBA
     * buffer. Returns empty vector if the file isn't a PNG, isn't
     * the expected size, or fails to load. */
    std::vector<uint8_t> decodePng(const std::string& path);

    std::vector<std::vector<uint8_t>> mFrames;
    size_t mCurrent = 0;
    int    mCallsSinceStep = 0;
};

/* Live framebuffer source: mmap()s /dev/shm/tmc_framebuffer (the
 * region tmc_pc populates when started with TMC_PUBLISH_FRAMEBUFFER=1).
 * Each `currentFrame()` reads the latest published bytes — no need
 * to advance(), the producer drives the cadence.
 *
 * Header layout MUST match port/port_shm_framebuffer.c:
 *   u32 magic = 'TMCF' (0x46434D54 LE)
 *   u32 version = 1
 *   u32 width
 *   u32 height
 *   u32 frameCount   (monotonic; used to detect a new frame)
 *   u32 pad
 *   u8  pixels[width * height * 4]
 *
 * Producer is single-writer, consumer is single-reader. Tearing is
 * possible (we copy `width*height*4` bytes while the producer might
 * be partway through writing the next frame), but unlikely to be
 * visible at 60fps; a proper SMR with double-buffered slots is a
 * future-work item. */
class ShmFrameSource {
public:
    /* Open the shm region. Returns false if the region doesn't exist
     * (tmc_pc not running, or built without the publisher) or has
     * the wrong magic/version. */
    bool open(const char* shmName = "/tmc_framebuffer");
    bool isOpen() const { return mBase != nullptr; }
    void close();

    int width()  const { return mWidth; }
    int height() const { return mHeight; }

    /* Returns the current pixel pointer + the producer's frame
     * counter at the moment of the call. The pointer is valid until
     * the next call OR until close() — caller should memcpy if they
     * need stable data across multiple ticks. */
    const uint8_t* currentFrame(uint32_t* outFrameSeq = nullptr) const;

    /* Raw OAM table (1024 bytes = 128 sprites × 4 halfwords each:
     * attr0, attr1, attr2, affine). Decoded by ParsedOam below.
     * nullptr if version < 2 / region too small. */
    const uint16_t* currentOam() const;
    int oamCount() const { return 128; }

    /* Per-BG-layer rendered planes (shm v3+). Each is a width×height
     * RGBA8 buffer rendered separately from the BG1 / BG2 layer (the
     * two main world layers in TMC). Returns nullptr when the
     * producer is older than v3. */
    const uint8_t* currentBgPlane(int planeIndex) const;

    /* OAM-only sprite plane (shm v4+). Width×height RGBA8 where pixels
     * outside sprite silhouettes are 0x00000000 (alpha=0). RT consumer
     * samples this for sprite-quad UVs so the any-hit alpha cutoff and
     * emissive-tag luma checks only consider real silhouette texels.
     * Returns nullptr when the producer is older than v4. */
    const uint8_t* currentSpritePlane() const;

private:
    void*    mBase    = nullptr;
    size_t   mBytes   = 0;
    int      mFd      = -1;
    int      mWidth   = 0;
    int      mHeight  = 0;
    uint32_t mVersion = 0;
};

/* One decoded OAM entry — what the rest of the demo needs to draw
 * the sprite as a 3D quad. Hidden / off-screen sprites are excluded
 * by ParsedOam::visibleSprites(). */
struct OamSprite {
    int     x, y;          /* screen position of the sprite's top-left */
    int     w, h;          /* sprite dimensions in pixels */
    uint8_t paletteIndex;  /* 4bpp palette bank, for future texturing */
    uint8_t priority;      /* 0..3, where 0 is in front */
    int     oamIndex;      /* original entry in the OAM table */
};

/* Decode the raw OAM halfwords into a list of visible sprites.
 * Pure host-side parsing; nothing here interacts with Vulkan. */
class ParsedOam {
public:
    /* Walk all 128 entries, append non-hidden ones to mSprites. */
    void parse(const uint16_t* oam, int count = 128);
    const std::vector<OamSprite>& visibleSprites() const { return mSprites; }
private:
    std::vector<OamSprite> mSprites;
};

}  /* namespace tmc_vkrt */
