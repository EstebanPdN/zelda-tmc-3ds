#include "config_values_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "config_values_3ds_test: check failed at line %d: %s\n", \
                    __LINE__, #condition);                                              \
            return EXIT_FAILURE;                                                       \
        }                                                                              \
    } while (0)

int main(void) {
    static const char* const aspectNames[] = { "wide", "original", "stretch" };
    static const char* const displayNames[] = {
        "blur", "bilinear", "ultra-sharp", "pixel-perfect"
    };
    static const char* const displayLabels[] = {
        "BLUR", "BILINEAR", "ULTRA SHARP", "PIXEL PERFECT"
    };

    CHECK(CONFIG_3DS_DEFAULT_ASPECT == PORT_3DS_ASPECT_STRETCH);
    CHECK(CONFIG_3DS_DEFAULT_DISPLAY == PORT_3DS_DISPLAY_BILINEAR);
    for (int i = 0; i < PORT_3DS_ASPECT_COUNT; ++i) {
        CHECK(strcmp(ConfigValues3DS_AspectName((Port3DSAspectRatio)i), aspectNames[i]) == 0);
        CHECK(ConfigValues3DS_ParseAspect(aspectNames[i]) == (Port3DSAspectRatio)i);
    }
    for (int i = 0; i < PORT_3DS_DISPLAY_COUNT; ++i) {
        CHECK(strcmp(ConfigValues3DS_DisplayName((Port3DSDisplayStyle)i), displayNames[i]) == 0);
        CHECK(strcmp(ConfigValues3DS_DisplayLabel((Port3DSDisplayStyle)i), displayLabels[i]) == 0);
        CHECK(ConfigValues3DS_ParseDisplay(displayNames[i]) == (Port3DSDisplayStyle)i);
    }
    CHECK(ConfigValues3DS_ParseAspect("invalid") == CONFIG_3DS_DEFAULT_ASPECT);
    CHECK(ConfigValues3DS_ParseDisplay("scaled") == CONFIG_3DS_DEFAULT_DISPLAY);
    CHECK(ConfigValues3DS_ParseDisplay("invalid") == CONFIG_3DS_DEFAULT_DISPLAY);

    puts("config_values_3ds_test: PASS");
    return EXIT_SUCCESS;
}
