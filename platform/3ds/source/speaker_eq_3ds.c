#include "speaker_eq_3ds.h"

#include <3ds.h>

bool Port_Config_SpeakerEq(void);
float Port_Config_SpeakerEqHz(void);

/* Channel 0 is the software mix, 1-4 the CGB offload, 5-12 the DirectSound PCM
 * offload. Every one of them must get the same treatment. */
#define SPEAKER_EQ_FIRST_CHANNEL 0
#define SPEAKER_EQ_LAST_CHANNEL 12

/* Butterworth: the flattest response without a resonant bump at the corner,
 * which is what you want for a corrective filter rather than an effect. */
#define SPEAKER_EQ_Q 0.7071f

static bool sHeadphones;
static bool sHeadphonesKnown;
static bool sActive;

static bool ShouldFilter(void) {
    if (!Port_Config_SpeakerEq()) return false;
    /* Headphones reproduce the low end the speakers cannot, so correcting for
     * the speakers would just thin them out. */
    return !sHeadphones;
}

void SpeakerEq3DS_ApplyToChannel(int channel) {
    if (channel < SPEAKER_EQ_FIRST_CHANNEL || channel > SPEAKER_EQ_LAST_CHANNEL) return;
    if (!ShouldFilter()) {
        ndspChnIirBiquadSetEnable(channel, false);
        return;
    }
    float hz = Port_Config_SpeakerEqHz();
    /* A cutoff at or above the band the game actually uses would gut the mix,
     * and one at zero would be a no-op with the filter still enabled. */
    if (hz < 40.0f) hz = 40.0f;
    if (hz > 1000.0f) hz = 1000.0f;
    if (!ndspChnIirBiquadSetParamsHighPassFilter(channel, hz, SPEAKER_EQ_Q)) {
        ndspChnIirBiquadSetEnable(channel, false);
        return;
    }
    ndspChnIirBiquadSetEnable(channel, true);
}

void SpeakerEq3DS_ApplyAll(void) {
    const bool wanted = ShouldFilter();
    for (int channel = SPEAKER_EQ_FIRST_CHANNEL; channel <= SPEAKER_EQ_LAST_CHANNEL; ++channel) {
        SpeakerEq3DS_ApplyToChannel(channel);
    }
    sActive = wanted;
}

/* DSP_GetHeadphoneStatus is a synchronous service IPC, and this runs from the
 * audio pump on the main thread. A dump measured that pump at 8.308 ms/frame
 * average against 0.011 ms in neighbouring runs, so putting a per-frame IPC on
 * that path is a risk taken for nothing: a jack does not need 60 Hz polling.
 * Every 32nd pump is roughly twice a second, which is imperceptible for
 * plugging in headphones and costs 1/32nd of the service traffic. */
#define SPEAKER_EQ_POLL_INTERVAL 32u

void SpeakerEq3DS_Poll(void) {
    static unsigned sTick;
    /* Always query on the very first call so init latches the real state. */
    if (sHeadphonesKnown && (sTick++ % SPEAKER_EQ_POLL_INTERVAL) != 0u) {
        return;
    }
    bool inserted = false;
    /* DSP_GetHeadphoneStatus is the authoritative jack state; osIsHeadsetConnected
     * reads the shared config page and does not track the plain audio jack on
     * every system version. Treat a failed query as "unknown, assume speakers"
     * so the correction still applies rather than silently disabling itself. */
    if (R_FAILED(DSP_GetHeadphoneStatus(&inserted))) {
        inserted = false;
    }
    if (sHeadphonesKnown && inserted == sHeadphones) return;
    sHeadphones = inserted;
    sHeadphonesKnown = true;
    SpeakerEq3DS_ApplyAll();
}

bool SpeakerEq3DS_Active(void) { return sActive; }
