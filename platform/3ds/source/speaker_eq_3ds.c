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

/*
 * Headphone state comes from the shared config page, which is a plain memory
 * read (os.h: OS_SharedConfig->headset_connected).
 *
 * It used to call DSP_GetHeadphoneStatus, which is IPC to the dsp sysmodule --
 * the same starved-core-1 round trip that made the audio buffer flush cost
 * ~330 us. Even throttled to every 32nd pump it showed up as 0.255 ms/frame
 * average in the pump (0.011 ms before it existed), i.e. ~8.2 ms per call, and
 * an 18.2 ms worst case. That was a quarter of the entire frame-rate deficit,
 * spent asking whether a jack was plugged in.
 *
 * Cheap enough to check every pump, so the throttle is gone too.
 */
void SpeakerEq3DS_Poll(void) {
    const bool inserted = osIsHeadsetConnected();
    if (sHeadphonesKnown && inserted == sHeadphones) return;
    sHeadphones = inserted;
    sHeadphonesKnown = true;
    SpeakerEq3DS_ApplyAll();
}

bool SpeakerEq3DS_Active(void) { return sActive; }
