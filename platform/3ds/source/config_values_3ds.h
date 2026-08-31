#ifndef CONFIG_VALUES_3DS_H
#define CONFIG_VALUES_3DS_H

#include "port_runtime_config.h"

#include <string.h>

#define CONFIG_3DS_DEFAULT_ASPECT PORT_3DS_ASPECT_STRETCH
#define CONFIG_3DS_DEFAULT_DISPLAY PORT_3DS_DISPLAY_BILINEAR

_Static_assert(PORT_3DS_DISPLAY_BLUR == 0 && PORT_3DS_DISPLAY_BILINEAR == 1 &&
                   PORT_3DS_DISPLAY_PIXEL_PERFECT == 2,
               "3DS display styles must follow the Settings menu order");

static inline const char* ConfigValues3DS_AspectName(Port3DSAspectRatio mode) {
    static const char* const names[PORT_3DS_ASPECT_COUNT] = {
        [PORT_3DS_ASPECT_WIDE] = "wide",
        [PORT_3DS_ASPECT_ORIGINAL] = "original",
        [PORT_3DS_ASPECT_STRETCH] = "stretch",
    };
    return mode >= 0 && mode < PORT_3DS_ASPECT_COUNT ? names[mode]
                                                     : names[CONFIG_3DS_DEFAULT_ASPECT];
}

static inline Port3DSAspectRatio ConfigValues3DS_ParseAspect(const char* value) {
    if (value != NULL) {
        for (int i = 0; i < PORT_3DS_ASPECT_COUNT; ++i) {
            if (strcmp(value, ConfigValues3DS_AspectName((Port3DSAspectRatio)i)) == 0) {
                return (Port3DSAspectRatio)i;
            }
        }
    }
    return CONFIG_3DS_DEFAULT_ASPECT;
}

static inline const char* ConfigValues3DS_DisplayName(Port3DSDisplayStyle style) {
    static const char* const names[PORT_3DS_DISPLAY_COUNT] = {
        [PORT_3DS_DISPLAY_BLUR] = "blur",
        [PORT_3DS_DISPLAY_BILINEAR] = "bilinear",
        [PORT_3DS_DISPLAY_PIXEL_PERFECT] = "pixel-perfect",
    };
    return style >= 0 && style < PORT_3DS_DISPLAY_COUNT ? names[style]
                                                        : names[CONFIG_3DS_DEFAULT_DISPLAY];
}

static inline const char* ConfigValues3DS_DisplayLabel(Port3DSDisplayStyle style) {
    static const char* const names[PORT_3DS_DISPLAY_COUNT] = {
        [PORT_3DS_DISPLAY_BLUR] = "BLUR",
        [PORT_3DS_DISPLAY_BILINEAR] = "BILINEAR",
        [PORT_3DS_DISPLAY_PIXEL_PERFECT] = "PIXEL PERFECT",
    };
    return style >= 0 && style < PORT_3DS_DISPLAY_COUNT ? names[style]
                                                        : names[CONFIG_3DS_DEFAULT_DISPLAY];
}

static inline Port3DSDisplayStyle ConfigValues3DS_ParseDisplay(const char* value) {
    if (value != NULL) {
        /* E9 exposed this fourth style. E10 deliberately folds existing
         * ultra-sharp preferences into the one supported filtered mode. */
        if (strcmp(value, "ultra-sharp") == 0)
            return PORT_3DS_DISPLAY_BILINEAR;
        for (int i = 0; i < PORT_3DS_DISPLAY_COUNT; ++i) {
            if (strcmp(value, ConfigValues3DS_DisplayName((Port3DSDisplayStyle)i)) == 0) {
                return (Port3DSDisplayStyle)i;
            }
        }
    }
    /* Scaled and malformed legacy values migrate to the new shipped default. */
    return CONFIG_3DS_DEFAULT_DISPLAY;
}

#endif
