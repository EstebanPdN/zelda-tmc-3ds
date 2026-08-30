#include "ndsp_pcm_offload.h"

#include <3ds.h>
#include <string.h>

#include <stdbool.h>
bool Port_Config_AudioDspInterpLinear(void);
void SpeakerEq3DS_ApplyToChannel(int channel);
#include <stddef.h>
bool Platform3DS_CleanDataCache(const void* addr, size_t size);

/* Channel 0 is the software mix and 1-4 are the CGB offload, so PCM starts
 * after those. MP2K's DirectSound polyphony is 8 in most games. */
/* NDSP has 24 channels: 0 is the software mix, 1-4 the CGB offload, so PCM can
 * take 5..16 and still leave headroom. MP2K's DirectSound polyphony runs to 12,
 * and a voice that finds no free channel falls back to software -- which is
 * exactly what full-offload coverage cannot afford. */
#define PCM_FIRST_CHANNEL 5
#define PCM_CHANNEL_COUNT 12

/* Samples must live in linear memory for the DSP to read them; the ROM copy is
 * in the application heap. Entries are reference counted and only an unused
 * entry is evictable -- a voice never has its sample pulled out from under it.
 * When nothing can be freed the voice simply stays in software. */
/* NDSP resamples in hardware but not without limit: it outputs at 32728 Hz and
 * a rate far above that yields garbage, not a high note. The CGB offload proved
 * this the loud way -- its noise voices asked for the LFSR clock, up to 524 kHz,
 * and the console played static. A voice cannot be handed back mid-note without
 * restarting it, so the check happens once at claim time and the voice stays in
 * software if it fails. */
#define PCM_MAX_RATE 32728.0f  /* NDSP output rate; see the PSG noise fault */

#define PCM_CACHE_ENTRIES 24
#define PCM_CACHE_BUDGET (512u * 1024u)

extern bool Port_Config_AudioDspPcm(void);

typedef struct {
    const int8_t* source; /* ROM-side key */
    int8_t* linear;
    uint32_t bytes;
    uint32_t refs;
    uint64_t lastUse;
} PcmCacheEntry;

static PcmCacheEntry sCache[PCM_CACHE_ENTRIES];
static uint32_t sCacheBytes;
static uint64_t sUseClock;

typedef struct {
    bool busy;
    PcmCacheEntry* entry;
    float rate;
    float leftVol;
    float rightVol;
    float masterLevel;
    ndspWaveBuf wave[2];
} PcmSlot;

static PcmSlot sSlots[PCM_CHANNEL_COUNT];
static bool sReady;
/* The software path applies the master fade to the track buffers after mixing,
 * which an offloaded voice never passes through. Rather than suspend offload
 * during a fade -- which would have to pull channels back mid-note and leave
 * every caller's slot index dangling -- the mixer publishes the level here and
 * it is folded into the channel mix. */
static float sMasterLevel = 1.0f;
static unsigned long long sVoicesOffloaded;
static unsigned long long sVoicesDeclined;
/* Voices the offload cannot take at all because of their type (compressed or
 * synth). Counted separately from capacity declines so the dump distinguishes
 * 'no channel free' from 'not implemented'. */
static unsigned long long sVoicesUnsupported;

bool NdspPcm_Available(void) { return sReady; }
unsigned long long NdspPcm_VoicesOffloaded(void) { return sVoicesOffloaded; }
unsigned long long NdspPcm_VoicesDeclined(void) { return sVoicesDeclined; }
unsigned long long NdspPcm_VoicesUnsupported(void) { return sVoicesUnsupported; }
void NdspPcm_NoteUnsupported(void) { ++sVoicesUnsupported; }

void NdspPcm_Init(void) {
    if (sReady || !Port_Config_AudioDspPcm()) return;
    memset(sCache, 0, sizeof(sCache));
    memset(sSlots, 0, sizeof(sSlots));
    sCacheBytes = 0;
    sUseClock = 0;
    sReady = true;
}

static void ReleaseEntry(PcmCacheEntry* entry) {
    if (entry && entry->refs) --entry->refs;
}

static void FreeEntry(PcmCacheEntry* entry) {
    if (!entry->linear) return;
    linearFree(entry->linear);
    sCacheBytes -= entry->bytes;
    memset(entry, 0, sizeof(*entry));
}

void NdspPcm_Shutdown(void) {
    for (int i = 0; i < PCM_CHANNEL_COUNT; ++i) {
        if (sSlots[i].busy) {
            ndspChnWaveBufClear(PCM_FIRST_CHANNEL + i);
            ReleaseEntry(sSlots[i].entry);
        }
        memset(&sSlots[i], 0, sizeof(sSlots[i]));
    }
    for (int i = 0; i < PCM_CACHE_ENTRIES; ++i) FreeEntry(&sCache[i]);
    sReady = false;
}

void NdspPcm_SetMasterLevel(float level) {
    sMasterLevel = level < 0.0f ? 0.0f : level;
}

static PcmCacheEntry* AcquireSample(const int8_t* source, uint32_t bytes) {
    if (!source || bytes == 0) return NULL;
    for (int i = 0; i < PCM_CACHE_ENTRIES; ++i) {
        if (sCache[i].linear && sCache[i].source == source &&
            sCache[i].bytes == bytes) {
            ++sCache[i].refs;
            sCache[i].lastUse = ++sUseClock;
            return &sCache[i];
        }
    }
    if (bytes > PCM_CACHE_BUDGET) return NULL;

    PcmCacheEntry* slot = NULL;
    for (int i = 0; i < PCM_CACHE_ENTRIES; ++i) {
        if (!sCache[i].linear) { slot = &sCache[i]; break; }
    }
    /* Evict least-recently-used unreferenced entries until it fits. */
    while ((!slot || sCacheBytes + bytes > PCM_CACHE_BUDGET)) {
        PcmCacheEntry* victim = NULL;
        for (int i = 0; i < PCM_CACHE_ENTRIES; ++i) {
            if (!sCache[i].linear || sCache[i].refs) continue;
            if (!victim || sCache[i].lastUse < victim->lastUse) victim = &sCache[i];
        }
        if (!victim) return NULL;
        FreeEntry(victim);
        if (!slot) slot = victim;
    }
    if (!slot) return NULL;

    slot->linear = (int8_t*)linearAlloc(bytes);
    if (!slot->linear) { memset(slot, 0, sizeof(*slot)); return NULL; }
    memcpy(slot->linear, source, bytes);
    /* Direct SVC rather than DSP_FlushDataCache's sysmodule round trip; this
     * runs on every newly claimed sample, i.e. constantly during music. */
    Platform3DS_CleanDataCache(slot->linear, bytes);
    slot->source = source;
    slot->bytes = bytes;
    slot->refs = 1;
    slot->lastUse = ++sUseClock;
    sCacheBytes += bytes;
    return slot;
}

static void ApplyMix(int chn, float leftVol, float rightVol) {
    float mix[12];
    memset(mix, 0, sizeof(mix));
    leftVol *= sMasterLevel;
    rightVol *= sMasterLevel;
    if (leftVol < 0.0f) leftVol = 0.0f;
    if (rightVol < 0.0f) rightVol = 0.0f;
    if (leftVol > 1.0f) leftVol = 1.0f;
    if (rightVol > 1.0f) rightVol = 1.0f;
    mix[0] = leftVol;
    mix[1] = rightVol;
    ndspChnSetMix(chn, mix);
}

bool NdspPcm_Play(int* slot, const int8_t* samplePtr, uint32_t endPos,
                  uint32_t loopPos, bool loopEnabled, uint32_t startPos,
                  float rate, float leftVol, float rightVol, bool* finished) {
    if (finished) *finished = false;
    if (!slot || !NdspPcm_Available()) return false;
    if (!samplePtr || endPos == 0 || rate <= 0.0f) return false;

    if (*slot < 0) {
        if (startPos >= endPos) return false;
        if (rate > PCM_MAX_RATE) return false;
        if (loopEnabled && loopPos >= endPos) return false;

        int free = -1;
        for (int i = 0; i < PCM_CHANNEL_COUNT; ++i) {
            if (!sSlots[i].busy) { free = i; break; }
        }
        if (free < 0) { ++sVoicesDeclined; return false; }

        PcmCacheEntry* entry = AcquireSample(samplePtr, endPos);
        if (!entry) { ++sVoicesDeclined; return false; }

        PcmSlot* s = &sSlots[free];
        const int chn = PCM_FIRST_CHANNEL + free;
        memset(s->wave, 0, sizeof(s->wave));
        ndspChnReset(chn);
        /* Match the software mix's channel 0, which upsamples 16364 -> 32728 Hz
         * with NDSP_INTERP_LINEAR. NDSP_INTERP_NONE is closer to MP2K's NEAREST
         * pitching taken alone, but it made offloaded voices brighter than the
         * software voices sharing the same mix. */
        ndspChnSetInterp(chn, Port_Config_AudioDspInterpLinear() ? NDSP_INTERP_LINEAR
                                                                : NDSP_INTERP_NONE);
        ndspChnSetFormat(chn, NDSP_FORMAT_MONO_PCM8);
        /* ndspChnReset above cleared the channel's IIR config. */
        SpeakerEq3DS_ApplyToChannel(chn);
        ndspChnSetRate(chn, rate);
        ApplyMix(chn, leftVol, rightVol);

        s->wave[0].data_pcm8 = (u8*)(entry->linear + startPos);
        s->wave[0].nsamples = (u32)(endPos - startPos);
        s->wave[0].looping = false;
        s->wave[0].status = NDSP_WBUF_FREE;
        ndspChnWaveBufAdd(chn, &s->wave[0]);
        if (loopEnabled) {
            s->wave[1].data_pcm8 = (u8*)(entry->linear + loopPos);
            s->wave[1].nsamples = (u32)(endPos - loopPos);
            s->wave[1].looping = true;
            s->wave[1].status = NDSP_WBUF_FREE;
            ndspChnWaveBufAdd(chn, &s->wave[1]);
        }
        s->busy = true;
        s->entry = entry;
        s->rate = rate;
        s->leftVol = leftVol;
        s->rightVol = rightVol;
        s->masterLevel = sMasterLevel;
        *slot = free;
        ++sVoicesOffloaded;
        return true;
    }

    if (*slot < 0 || *slot >= PCM_CHANNEL_COUNT) return false;
    PcmSlot* s = &sSlots[*slot];
    if (!s->busy) return false;
    const int chn = PCM_FIRST_CHANNEL + *slot;
    /* A non-looping sample that has run out is finished; let the caller's
     * envelope carry on but stop claiming the channel. */
    if (!s->wave[1].nsamples && s->wave[0].status == NDSP_WBUF_DONE) {
        /* The software path calls Kill() when a non-looping sample runs out.
         * Report it so the caller does the same: falling back to the software
         * mixer here would restart the sample, because MP2K's samplePos has
         * not advanced while the DSP owned the voice. */
        NdspPcm_Stop(slot);
        if (finished) *finished = true;
        return false;
    }
    if (rate > PCM_MAX_RATE) rate = PCM_MAX_RATE;
    if (rate != s->rate) {
        ndspChnSetRate(chn, rate);
        s->rate = rate;
    }
    if (leftVol != s->leftVol || rightVol != s->rightVol ||
        sMasterLevel != s->masterLevel) {
        ApplyMix(chn, leftVol, rightVol);
        s->leftVol = leftVol;
        s->rightVol = rightVol;
        s->masterLevel = sMasterLevel;
    }
    return true;
}

void NdspPcm_Stop(int* slot) {
    if (!slot || *slot < 0 || *slot >= PCM_CHANNEL_COUNT) { if (slot) *slot = -1; return; }
    PcmSlot* s = &sSlots[*slot];
    if (s->busy) {
        ndspChnWaveBufClear(PCM_FIRST_CHANNEL + *slot);
        ReleaseEntry(s->entry);
    }
    memset(s, 0, sizeof(*s));
    *slot = -1;
}
