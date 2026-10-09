/**
 * @file port_ua_splash.h
 * @brief Ukrainian boot logo for the 3DS splash screen (tloz-tmc-ua).
 *
 * The splash is drawn before the ROM is loaded, so Port_IsUkrainianRom()
 * cannot be used yet. Instead peek at the marker of the .gba files in the
 * port's SD card folder: if one of them is the Ukrainian ROM, show
 * romfs:/splash-ua.rgb565 ("ЛЕГЕНДА ПРО ЗЕЛЬДУ / Диво-Ковпак") instead of the
 * upstream logo. Any other ROM, or no ROM at all, keeps the upstream splash.
 *
 * romfs/splash-ua.rgb565 is generated from assets/splash-ua.png by
 * ua/make_splash.py.
 */
#ifndef PORT_UA_SPLASH_H
#define PORT_UA_SPLASH_H

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "port_ua_marker.h"

#define PORT_UA_SPLASH_DIR "sdmc:/3ds/The Minish Cap 3DS"
#define PORT_UA_SPLASH_PATH "romfs:/splash-ua.rgb565"

static inline int Port_UA_FileHasMarker(const char* path) {
    char marker[PORT_UA_MARKER_LEN];
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fseek(file, PORT_UA_MARKER_OFFSET, SEEK_SET) == 0 &&
             fread(marker, 1, sizeof(marker), file) == sizeof(marker) &&
             memcmp(marker, PORT_UA_MARKER, PORT_UA_MARKER_LEN) == 0;
    fclose(file);
    return ok;
}

/** Splash file to show: the Ukrainian one when a Ukrainian ROM is present. */
static inline const char* Port_UA_SplashPath(const char* fallback) {
    DIR* dir = opendir(PORT_UA_SPLASH_DIR);
    if (!dir) return fallback;
    const char* result = fallback;
    struct dirent* entry;
    char path[512];
    while ((entry = readdir(dir)) != NULL) {
        const size_t length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || length < 5) continue;
        const char* ext = entry->d_name + length - 4;
        if (ext[0] != '.' || tolower((unsigned char)ext[1]) != 'g' ||
            tolower((unsigned char)ext[2]) != 'b' || tolower((unsigned char)ext[3]) != 'a')
            continue;
        snprintf(path, sizeof(path), "%s/%s", PORT_UA_SPLASH_DIR, entry->d_name);
        if (Port_UA_FileHasMarker(path)) {
            result = PORT_UA_SPLASH_PATH;
            break;
        }
    }
    closedir(dir);
    return result;
}

#endif /* PORT_UA_SPLASH_H */
