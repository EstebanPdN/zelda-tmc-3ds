/*
 * port/port_rom_picker.c — SDL3 file-picker fallback for "no ROM
 * found." Lets the user point at any .gba file without having to
 * manually rename and place it. We validate the picked file is a
 * known Minish Cap ROM (SHA-1 + SHA-256), then install it as
 * baserom.gba — next to the executable on desktop, in the app data
 * dir (the CWD) on Android — so subsequent launches skip the prompt.
 *
 * The picked path is read through SDL_LoadFile: on Android the SAF
 * picker returns content:// URIs that plain fopen cannot open, and
 * SDL_IOFromFile routes those through ContentResolver.
 */
#define _DEFAULT_SOURCE 1 /* readlink(2) on glibc */
#define _BSD_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "port_rom_picker.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_messagebox.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#define _GNU_SOURCE
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Outcome of the async picker callback, published cross-thread: on
 * Android the callback runs on the Java UI thread (onActivityResult)
 * while the SDL main thread spins in the wait loop below. SDL atomics
 * give the release/acquire ordering `volatile` does not. Path bytes
 * are written before the status flips to a terminal state. */
enum {
    PICK_PENDING = 0,
    PICK_CANCELLED,
    PICK_SUCCESS,
    PICK_FAILED,
};

static SDL_AtomicInt sPickStatus;
static char sPickPath[4096];

static void SDLCALL RomPickerCallback(void* userdata, const char* const* filelist, int filter) {
    (void)userdata;
    (void)filter;
    if (!filelist) {
        SDL_SetAtomicInt(&sPickStatus, PICK_FAILED);
        return;
    }
    if (!filelist[0]) {
        SDL_SetAtomicInt(&sPickStatus, PICK_CANCELLED);
        return;
    }
    strncpy(sPickPath, filelist[0], sizeof(sPickPath) - 1);
    sPickPath[sizeof(sPickPath) - 1] = '\0';
    SDL_SetAtomicInt(&sPickStatus, PICK_SUCCESS);
}

/* ------------------------------------------------------------------
 * SHA-1 — minimal RFC 3174 reference impl, public-domain style.
 * Used to identify a picked .gba file by hash rather than filename,
 * so the user can name their ROM anything and we still recognise
 * a valid TMC dump.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t buffer[64];
    uint8_t buflen;
} SHA1_CTX;

static uint32_t sha1_rol(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void sha1_init(SHA1_CTX* c) {
    c->state[0] = 0x67452301u;
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;
    c->bitcount = 0;
    c->buflen = 0;
}

static void sha1_transform(SHA1_CTX* c, const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2];
    uint32_t d = c->state[3], e = c->state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & cc) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ cc ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = sha1_rol(b, 30);
        b = a;
        a = t;
    }
    c->state[0] += a;
    c->state[1] += b;
    c->state[2] += cc;
    c->state[3] += d;
    c->state[4] += e;
}

static void sha1_update(SHA1_CTX* c, const uint8_t* data, size_t len) {
    c->bitcount += (uint64_t)len * 8;
    while (len > 0) {
        size_t want = 64 - c->buflen;
        size_t take = len < want ? len : want;
        memcpy(c->buffer + c->buflen, data, take);
        c->buflen += (uint8_t)take;
        data += take;
        len -= take;
        if (c->buflen == 64) {
            sha1_transform(c, c->buffer);
            c->buflen = 0;
        }
    }
}

static void sha1_final(SHA1_CTX* c, uint8_t out[20]) {
    uint64_t bits = c->bitcount;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    pad = 0x00;
    while (c->buflen != 56)
        sha1_update(c, &pad, 1);
    uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i)
        lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_update(c, lenbe, 8);
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
}

static void Sha1HexOfBuffer(const void* data, size_t len, char hex_out[41]) {
    SHA1_CTX c;
    sha1_init(&c);
    sha1_update(&c, (const uint8_t*)data, len);
    uint8_t digest[20];
    sha1_final(&c, digest);
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 20; ++i) {
        hex_out[i * 2] = kHex[digest[i] >> 4];
        hex_out[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    hex_out[40] = '\0';
}

/* ------------------------------------------------------------------
 * SHA-256 — minimal FIPS 180-4 reference impl, public-domain style.
 * SHA-1 (above) stays as a fast identity pre-filter, but SHA-1 is
 * collision-broken; requiring a SHA-256 match too makes a recognised
 * ROM's identity cryptographically strong (a forged file would have to
 * collide on BOTH, which is infeasible).
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[64];
    uint8_t buflen;
} SHA256_CTX;

static uint32_t sha256_ror(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
}

static void sha256_init(SHA256_CTX* c) {
    c->state[0] = 0x6a09e667u;
    c->state[1] = 0xbb67ae85u;
    c->state[2] = 0x3c6ef372u;
    c->state[3] = 0xa54ff53au;
    c->state[4] = 0x510e527fu;
    c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu;
    c->state[7] = 0x5be0cd19u;
    c->bitcount = 0;
    c->buflen = 0;
}

static void sha256_transform(SHA256_CTX* c, const uint8_t block[64]) {
    static const uint32_t kK[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = sha256_ror(w[i - 15], 7) ^ sha256_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_ror(w[i - 2], 17) ^ sha256_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = sha256_ror(e, 6) ^ sha256_ror(e, 11) ^ sha256_ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + kK[i] + w[i];
        uint32_t S0 = sha256_ror(a, 2) ^ sha256_ror(a, 13) ^ sha256_ror(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = cc;
        cc = b;
        b = a;
        a = t1 + t2;
    }
    c->state[0] += a;
    c->state[1] += b;
    c->state[2] += cc;
    c->state[3] += d;
    c->state[4] += e;
    c->state[5] += f;
    c->state[6] += g;
    c->state[7] += h;
}

static void sha256_update(SHA256_CTX* c, const uint8_t* data, size_t len) {
    c->bitcount += (uint64_t)len * 8;
    while (len > 0) {
        size_t want = 64 - c->buflen;
        size_t take = len < want ? len : want;
        memcpy(c->buffer + c->buflen, data, take);
        c->buflen += (uint8_t)take;
        data += take;
        len -= take;
        if (c->buflen == 64) {
            sha256_transform(c, c->buffer);
            c->buflen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX* c, uint8_t out[32]) {
    uint64_t bits = c->bitcount;
    uint8_t pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0x00;
    while (c->buflen != 56)
        sha256_update(c, &pad, 1);
    uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i)
        lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    sha256_update(c, lenbe, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
}

static void Sha256HexOfBuffer(const void* data, size_t len, char hex_out[65]) {
    SHA256_CTX c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t*)data, len);
    uint8_t digest[32];
    sha256_final(&c, digest);
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex_out[i * 2] = kHex[digest[i] >> 4];
        hex_out[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    hex_out[64] = '\0';
}

/* Known TMC ROM hashes. The picker recognises a file as a TMC ROM if its
 * SHA-1 matches; for dumps with a known SHA-256 it must match that too
 * (SHA-1 alone is collision-broken). Filename is irrelevant. */
static const struct {
    const char* sha1;
    const char* sha256; /* "" = not yet known (demo dumps): SHA-1 only */
    const char* region;
} kKnownTmcRoms[] = {
    { "b4bd50e4131b027c334547b4524e2dbbd4227130", "bedc74df62755f705398273de8ed3bc59be610cf55760d0b9aa277f1f5035e73",
      "USA" },
    { "cff199b36ff173fb6faf152653d1bccf87c26fb7", "c84645731952b7677f514ae222683504066334ab2af904e64a8a84ffc1af46c6",
      "EU" },
    { "6c5404a1effb17f481f352181d0f1c61a2765c5d", "16ac2572ba17e9cb2a70093d41f97ef8cff66c56417e45ea98adacdc51bb4b38",
      "JP" },
    { "63fcad218f9047b6a9edbb68c98bd0dec322d7a1", "", "USA Demo" },
    { "9cdb56fa79bba13158b81925c1f3641251326412", "", "JP Demo" },
};

/* Validate that `data` is a real TMC ROM by hash. Returns the region
 * tag ("USA" / "EU" / "JP" / "USA Demo" / "JP Demo") or NULL.
 *
 * Filename is intentionally irrelevant — users pick whatever they
 * named their dump. A fast size pre-check (16 MiB ± slop, 4 MiB
 * demos) rules out obvious junk before we pay for the hashes. */
static const char* ValidateRomBuffer(const void* data, size_t len) {
    if (len < 0x00200000 || len > 0x01010000)
        return NULL;

    char hex[41];
    Sha1HexOfBuffer(data, len, hex);
    for (size_t i = 0; i < sizeof(kKnownTmcRoms) / sizeof(kKnownTmcRoms[0]); ++i) {
        if (strcmp(hex, kKnownTmcRoms[i].sha1) != 0)
            continue;
        /* SHA-1 matched. SHA-1 is collision-broken, so when we have a
         * SHA-256 for this dump require it to match too — a forged file
         * cannot collide on both. (Demo dumps have no SHA-256: SHA-1 only.) */
        if (kKnownTmcRoms[i].sha256 && kKnownTmcRoms[i].sha256[0]) {
            char hex256[65];
            Sha256HexOfBuffer(data, len, hex256);
            if (strcmp(hex256, kKnownTmcRoms[i].sha256) != 0)
                return NULL;
        }
        return kKnownTmcRoms[i].region;
    }
    return NULL;
}

#ifndef __ANDROID__

static int GetExeDir(char* out, size_t out_len) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_len);
    if (n == 0 || n >= out_len)
        return -1;
    char* slash = strrchr(out, '\\');
    if (!slash)
        slash = strrchr(out, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    return 0;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)out_len;
    if (_NSGetExecutablePath(out, &sz) != 0)
        return -1;
    char* slash = strrchr(out, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, out_len - 1);
    if (n <= 0)
        return -1;
    out[n] = '\0';
    char* slash = strrchr(out, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    return 0;
#endif
}

#endif /* !__ANDROID__ */

/* Resolve where the picked ROM gets installed. Desktop: next to the
 * executable, matching the release-tarball layout that
 * Port_FindBaseRomPath probes. Android: the CWD — port_main.c chdir'd
 * to the app's files dir before any of this runs, and the bare
 * "baserom.gba" candidate resolves there. The path must be ABSOLUTE:
 * SDL_IOFromFile on Android resolves relative paths against internal
 * storage (SDL_GetAndroidInternalStoragePath), not the CWD, so a bare
 * "baserom.gba" would be written where fopen-based probing never
 * looks. (The exe dir on Android is an unwritable system path, and
 * the picked file is a content:// URI that only SDL_IOFromFile can
 * read anyway.) */
static int ResolveInstallPath(char* dst, size_t dst_len) {
#ifdef __ANDROID__
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
        return -1;
    snprintf(dst, dst_len, "%s/baserom.gba", cwd);
    return 0;
#else
    char exedir[4096];
    if (GetExeDir(exedir, sizeof(exedir)) != 0)
        return -1;
    snprintf(dst, dst_len, "%s%cbaserom.gba", exedir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );
    return 0;
#endif
}

/* Returns 0 if the user picked a valid ROM and we installed it as
 * baserom.gba (so the caller can re-run Port_FindBaseRomPath and
 * find it). Returns -1 on cancel/error.
 *
 * The prelaunch UI shows the user-facing "Pick your ROM" prompt
 * before calling this, so we skip straight to the file dialog. */
int Port_RomPicker_PromptAndInstall(void) {
    fprintf(stderr, "[rom-picker] opening SDL file dialog...\n");

    SDL_DialogFileFilter filters[] = {
        { "Game Boy Advance ROM", "gba" },
        { "All files", "*" },
    };

    SDL_SetAtomicInt(&sPickStatus, PICK_PENDING);
    sPickPath[0] = '\0';
    SDL_ShowOpenFileDialog(RomPickerCallback, NULL, NULL, filters, 2, NULL, false);

    /* Block until the user picks (or cancels). The picker is async on
     * every backend — pump events here so platform-native code can
     * deliver the callback (on Android it arrives via onActivityResult
     * after a round-trip through the system documents UI). */
    while (SDL_GetAtomicInt(&sPickStatus) == PICK_PENDING) {
        SDL_PumpEvents();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                SDL_SetAtomicInt(&sPickStatus, PICK_CANCELLED);
                break;
            }
        }
        SDL_Delay(16);
    }

    if (SDL_GetAtomicInt(&sPickStatus) != PICK_SUCCESS) {
        return -1;
    }

    /* One read, validate in memory, one write. SDL_LoadFile routes
     * through SDL_IOFromFile: plain paths everywhere, content:// URIs
     * on Android (SAF), UTF-8 -> UTF-16 paths on Windows. A TMC dump
     * is at most 16 MiB, so the buffer is cheap and transient. */
    size_t romLen = 0;
    void* romData = SDL_LoadFile(sPickPath, &romLen);
    if (!romData) {
        char msg[4608];
        snprintf(msg, sizeof(msg), "Could not read the picked file:\n%s\n\n%s", sPickPath, SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Read failed", msg, NULL);
        return -1;
    }

    const char* region = ValidateRomBuffer(romData, romLen);
    if (!region) {
        char hex[41];
        Sha1HexOfBuffer(romData, romLen, hex);
        SDL_free(romData);
        char msg[5120];
        snprintf(msg, sizeof(msg),
                 "That file's SHA-1 doesn't match any known TMC ROM:\n\n"
                 "  Path:     %s\n"
                 "  SHA-1:    %s\n\n"
                 "Accepted hashes:\n"
                 "  USA      b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                 "  EU       cff199b36ff173fb6faf152653d1bccf87c26fb7\n"
                 "  JP       6c5404a1effb17f481f352181d0f1c61a2765c5d\n"
                 "  USA Demo 63fcad218f9047b6a9edbb68c98bd0dec322d7a1\n"
                 "  JP Demo  9cdb56fa79bba13158b81925c1f3641251326412\n\n"
                 "If your dump matches one of these by hash but the picker\n"
                 "still rejects it, something's mangling the bytes on disk.",
                 sPickPath, hex);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wrong ROM", msg, NULL);
        return -1;
    }

    /* Install as baserom.gba (overwrite if already exists). Once
     * written, Port_FindBaseRomPath picks it up via the usual
     * candidate list. */
    char dst[4096];
    if (ResolveInstallPath(dst, sizeof(dst)) != 0) {
        SDL_free(romData);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Install failed",
                                 "Could not locate this executable's directory.", NULL);
        return -1;
    }

    const bool wrote = SDL_SaveFile(dst, romData, romLen);
    SDL_free(romData);
    if (!wrote) {
        char msg[4608];
        snprintf(msg, sizeof(msg), "Could not write %s:\n%s", dst, SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Install failed", msg, NULL);
        return -1;
    }

    fprintf(stderr, "[rom-picker] installed %s ROM -> %s\n", region, dst);
    return 0;
}
