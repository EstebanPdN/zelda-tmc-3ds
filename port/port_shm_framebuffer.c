/*
 * port/port_shm_framebuffer.c
 *
 * Publishes per-frame ViruaPPU state to a POSIX shared-memory region
 * on Linux. The Vulkan-RT scaffold in port/vk_rt_experiment/ mmaps
 * the same region and consumes it for "real engine integration" —
 * each OAM sprite becomes its own 3D quad, the framebuffer goes
 * behind as the BG plane.
 *
 * Shared-memory layout (at `/dev/shm/tmc_framebuffer`, version 2):
 *
 *     offset 0..3     uint32_t magic     = 0x54_4D_43_46 ("TMCF")
 *     offset 4..7     uint32_t version   = 2
 *     offset 8..11    uint32_t width
 *     offset 12..15   uint32_t height
 *     offset 16..19   uint32_t frameCount  (monotonic; consumers poll)
 *     offset 20..23   uint32_t oamCount    = 128
 *     offset 24..PIX  uint8_t  pixels[width * height * 4]  (RGBA8)
 *     offset PIX..PIX+1023  uint16_t oam[512]  (raw GBA OAM:
 *                            128 sprites × 4 halfwords each =
 *                            attr0, attr1, attr2, affine padding)
 *
 * Where PIX = 24 + width*height*4. Total for the GBA 240×160 layout:
 *   24 (header) + 153600 (pixels) + 1024 (OAM) = 154648 bytes.
 *
 * Activated by setting TMC_PUBLISH_FRAMEBUFFER=1 in the environment.
 * No-ops otherwise so the normal build is unaffected. Linux only —
 * skipped on Windows/macOS (the macros guard the bodies).
 *
 * Single-writer (tmc_pc), single-reader (vkrt_demo). Tearing is
 * possible but unlikely at 60Hz; bump to a double-buffered slot
 * design when the demo cares about pixel-perfect frame coherence.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && !defined(__ANDROID__)
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#define TMC_SHM_MAGIC   0x46434D54u  /* "TMCF" little-endian */
#define TMC_SHM_VERSION 4u           /* v4 adds OAM-only sprite plane (plane index 2) */
#define TMC_SHM_HEADER  24
#define TMC_SHM_OAM_BYTES 1024u       /* 128 OAM entries × 8 bytes */
#define TMC_SHM_BG_PLANES 3u          /* BG1 + BG2 + Sprite (OAM-only, alpha=0 outside silhouettes) */
#define TMC_SHM_PATH    "/tmc_framebuffer"

#if defined(__linux__) && !defined(__ANDROID__)

static int      sShmFd     = -1;
static uint8_t* sShmBase   = NULL;
static size_t   sShmBytes  = 0;
static int      sEnabled   = -1;   /* -1 = not yet checked, 0 = off, 1 = on */
static uint32_t sFrameSeq  = 0;

/* Public probe — port_ppu.cpp checks this to decide whether to spend
 * cycles on the per-BG re-render. Mirrors shmEnabled() but as a real
 * symbol the C++ TU can extern. */
int Port_Shm_IsActive(void) {
#if defined(__linux__) && !defined(__ANDROID__)
    extern int shmEnabled(void);
    return shmEnabled();
#else
    return 0;
#endif
}

/* Lazily probe TMC_PUBLISH_FRAMEBUFFER on first call. Avoids paying
 * the getenv lookup every frame. */
int shmEnabled(void) {
    if (sEnabled < 0) {
        const char* v = getenv("TMC_PUBLISH_FRAMEBUFFER");
        sEnabled = (v && v[0] && v[0] != '0') ? 1 : 0;
        if (sEnabled) {
            fprintf(stderr, "[shm-fb] publish enabled (TMC_PUBLISH_FRAMEBUFFER=%s)\n", v);
        }
    }
    return sEnabled;
}

/* Set up the shm object on first frame, then keep it open. Returns
 * 0 on success, -1 on failure (publishing silently no-ops for the
 * rest of the run). */
static int shmInit(int width, int height) {
    if (sShmBase) return 0;  /* already up */

    const size_t planeBytes = (size_t)width * (size_t)height * 4u;
    const size_t bytes = (size_t)TMC_SHM_HEADER
                       + planeBytes                       /* composite framebuffer */
                       + (size_t)TMC_SHM_OAM_BYTES        /* OAM table */
                       + planeBytes * (size_t)TMC_SHM_BG_PLANES; /* BG1, BG2 planes */
    sShmFd = shm_open(TMC_SHM_PATH, O_CREAT | O_RDWR, 0600);
    if (sShmFd < 0) {
        fprintf(stderr, "[shm-fb] shm_open failed: %s\n", strerror(errno));
        sEnabled = 0;
        return -1;
    }
    if (ftruncate(sShmFd, (off_t)bytes) < 0) {
        fprintf(stderr, "[shm-fb] ftruncate failed: %s\n", strerror(errno));
        close(sShmFd);
        sShmFd = -1;
        sEnabled = 0;
        return -1;
    }
    sShmBase = (uint8_t*)mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                              MAP_SHARED, sShmFd, 0);
    if (sShmBase == MAP_FAILED) {
        fprintf(stderr, "[shm-fb] mmap failed: %s\n", strerror(errno));
        close(sShmFd);
        sShmFd = -1;
        sShmBase = NULL;
        sEnabled = 0;
        return -1;
    }
    sShmBytes = bytes;

    /* Initialise the header. Consumers verify magic + version before
     * trusting the rest. */
    uint32_t* h = (uint32_t*)sShmBase;
    h[0] = TMC_SHM_MAGIC;
    h[1] = TMC_SHM_VERSION;
    h[2] = (uint32_t)width;
    h[3] = (uint32_t)height;
    h[4] = 0;       /* frameCount */
    h[5] = 128u;    /* oamCount (fixed for GBA) */

    fprintf(stderr, "[shm-fb] mapped %zu bytes at %s (%dx%d RGBA8)\n",
            bytes, TMC_SHM_PATH, width, height);
    return 0;
}

void Port_Shm_PublishFramebuffer(const uint32_t* pixels, int width, int height) {
    if (!shmEnabled()) return;
    if (!pixels || width <= 0 || height <= 0) return;

    if (!sShmBase) {
        if (shmInit(width, height) != 0) return;
    }
    uint32_t* h = (uint32_t*)sShmBase;
    if ((int)h[2] != width || (int)h[3] != height) {
        h[2] = (uint32_t)width;
        h[3] = (uint32_t)height;
    }

    const size_t payload = (size_t)width * (size_t)height * 4u;
    if (TMC_SHM_HEADER + payload + TMC_SHM_OAM_BYTES > sShmBytes) return;

    /* Pixel block (variable-size by w*h). */
    memcpy(sShmBase + TMC_SHM_HEADER, pixels, payload);

    /* OAM block — copied straight from the GBA-format gOamMem array
     * (1024 bytes = 128 sprites × 8 bytes). The consumer parses the
     * 4-halfword-per-sprite attribute layout itself. We pull it
     * here, in the same writer thread as the framebuffer, so the
     * two blocks are coherent for the consumer's frameCount check. */
    extern uint16_t gOamMem[];
    memcpy(sShmBase + TMC_SHM_HEADER + payload, gOamMem, TMC_SHM_OAM_BYTES);

    /* Increment the monotonic counter LAST so consumers polling the
     * frame counter only see complete frames. Single-writer/single-
     * reader; weaker than a proper SMR but enough for prototyping. */
    __atomic_store_n(&h[4], ++sFrameSeq, __ATOMIC_RELEASE);
}

/* Publish two pre-rendered BG-layer planes (BG1, BG2) for the RT
 * scaffold to render as separate world-Z quads. Each plane is the
 * same dimensions as the composite framebuffer (width × height
 * RGBA8). The caller is responsible for actually doing the per-line
 * render (via virtuappu_mode1_render_text_bg_line); this function
 * just copies the prepared planes into the shm region.
 *
 * Layout: planes go AFTER the composite + OAM blocks. The header's
 * width/height applies to all planes uniformly. */
void Port_Shm_PublishBgPlanes(const uint32_t* bg1, const uint32_t* bg2,
                              int width, int height) {
    if (!shmEnabled() || !sShmBase) return;
    if (!bg1 || !bg2 || width <= 0 || height <= 0) return;

    const size_t planeBytes = (size_t)width * (size_t)height * 4u;
    const size_t off0 = (size_t)TMC_SHM_HEADER + planeBytes + (size_t)TMC_SHM_OAM_BYTES;
    const size_t off1 = off0 + planeBytes;

    if (off1 + planeBytes > sShmBytes) return;

    memcpy(sShmBase + off0, bg1, planeBytes);
    memcpy(sShmBase + off1, bg2, planeBytes);
    /* No frame counter bump — the framebuffer publish flips it.
     * Call this BEFORE PublishFramebuffer so planes + composite stay
     * coherent for the consumer's poll. */
}

/* Publish an OAM-only sprite plane (shm v4+). The plane is the
 * engine's OBJ-layer render with NO background — pixels outside
 * sprite silhouettes are 0x00000000 (alpha=0). Lets the RT consumer
 * sample real sprite alpha for shadow-occluder testing and apply
 * emissive boosts only to actual silhouette pixels (rather than the
 * fully-opaque composite, which would light whole 16×16 quad
 * rectangles around any glowing sprite).
 *
 * Layout: appended after the 2 BG planes (index 2 in the plane
 * array). Caller still drives cadence — call BEFORE PublishFramebuffer
 * so the sprite plane is coherent with the composite + OAM table. */
void Port_Shm_PublishSpritePlane(const uint32_t* sprites,
                                 int width, int height) {
    if (!shmEnabled() || !sShmBase) return;
    if (!sprites || width <= 0 || height <= 0) return;

    const size_t planeBytes = (size_t)width * (size_t)height * 4u;
    const size_t off2 = (size_t)TMC_SHM_HEADER + planeBytes + (size_t)TMC_SHM_OAM_BYTES
                       + planeBytes * 2u;  /* skip BG1, BG2 */

    if (off2 + planeBytes > sShmBytes) return;

    memcpy(sShmBase + off2, sprites, planeBytes);
}

void Port_Shm_Shutdown(void) {
    if (sShmBase && sShmBase != MAP_FAILED) {
        munmap(sShmBase, sShmBytes);
    }
    if (sShmFd >= 0) {
        close(sShmFd);
        shm_unlink(TMC_SHM_PATH);
    }
    sShmBase = NULL;
    sShmFd = -1;
    sShmBytes = 0;
}

#else  /* non-Linux */

int Port_Shm_IsActive(void) { return 0; }
int shmEnabled(void) { return 0; }
void Port_Shm_PublishFramebuffer(const uint32_t* p, int w, int h) {
    (void)p; (void)w; (void)h;
}
void Port_Shm_PublishBgPlanes(const uint32_t* a, const uint32_t* b, int w, int h) {
    (void)a; (void)b; (void)w; (void)h;
}
void Port_Shm_PublishSpritePlane(const uint32_t* s, int w, int h) {
    (void)s; (void)w; (void)h;
}
void Port_Shm_Shutdown(void) { }

#endif
