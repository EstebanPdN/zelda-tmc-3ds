#include "platform_3ds.h"

#include "port_audio.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_runtime_config.h"
#include "port_russian_3ds.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define APP_DIR "sdmc:/3ds/The Minish Cap 3DS"
#define ROM_PATH_SIZE 512

extern void AgbMain(void);

static int PrepareStorage(void) {
    mkdir("sdmc:/3ds", 0777);
    if (mkdir(APP_DIR, 0777) != 0 && errno != EEXIST) return 0;
    return chdir(APP_DIR) == 0;
}

static int HasGbaExtension(const char* name) {
    const size_t length = strlen(name);
    if (length < 5) return 0;
    const char* ext = name + length - 4;
    return tolower((unsigned char)ext[0]) == '.' &&
           tolower((unsigned char)ext[1]) == 'g' &&
           tolower((unsigned char)ext[2]) == 'b' &&
           tolower((unsigned char)ext[3]) == 'a';
}

static int RomIsSupported(const char* path) {
    char gameCode[4];
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fseek(file, 0xAC, SEEK_SET) == 0 && fread(gameCode, 1, sizeof(gameCode), file) == sizeof(gameCode) &&
             (memcmp(gameCode, "BZME", sizeof(gameCode)) == 0 || memcmp(gameCode, "BZMP", sizeof(gameCode)) == 0);
    fclose(file);
    return ok;
}

static int FindRom(char* out, size_t outSize) {
    DIR* dir = opendir(".");
    if (!dir) return 0;

    int foundGba = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!HasGbaExtension(entry->d_name)) continue;

        struct stat info;
        if (stat(entry->d_name, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        foundGba = 1;
        if (!RomIsSupported(entry->d_name)) continue;
        snprintf(out, outSize, "%s", entry->d_name);
        closedir(dir);
        return 1;
    }

    closedir(dir);
    return foundGba ? -1 : 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!Platform3DS_Init()) return 1;
    Platform3DS_ShowSplash();

    printf("The Minish Cap 3DS v" TMC_PORT_VERSION "\n\n");
    printf("System: %s\n", Platform3DS_IsNew3DS() ? "New Nintendo 3DS" : "Nintendo 3DS");
    printf("PPU worker core 1: %u%%\n", Platform3DS_Core1TimeLimit());
    printf("Extra New 3DS core: %s\n\n", Platform3DS_IsNew3DS() ? "enabled" : "unavailable");
    printf("Preparing storage...\n");
    if (!PrepareStorage()) {
        Platform3DS_ShowFatal("Storage error", "Could not open " APP_DIR ".");
        Platform3DS_Shutdown();
        return 1;
    }

    char romPath[ROM_PATH_SIZE];
    int romResult = FindRom(romPath, sizeof(romPath));
    if (romResult <= 0) {
        char message[512];
        if (romResult < 0) {
            snprintf(message, sizeof(message),
                     "None of the .gba files in:\n%s\n"
                     "is a supported USA (BZME) or Europe (BZMP) ROM.\n\n"
                     "Expected SHA-1:\nUSA: b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                     "Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7",
                     APP_DIR);
        } else {
            snprintf(message, sizeof(message),
                     "Copy your clean USA or Europe ROM to:\n%s\n\nAny .gba filename is accepted.\n\n"
                     "Expected SHA-1:\nUSA: b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                     "Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7",
                     APP_DIR);
        }
        Platform3DS_ShowFatal(romResult < 0 ? "Unsupported ROM" : "ROM not found", message);
        Platform3DS_Shutdown();
        return 1;
    }

    FILE* rom = fopen(romPath, "rb");
    if (!rom) {
        Platform3DS_ShowFatal("ROM error", "Could not open the selected .gba file.");
        Platform3DS_Shutdown();
        return 1;
    }
    fclose(rom);

    printf("Loading ROM and tables...\n");
    Port_Config_Load("tmc3ds.ini");
    Port_LoadRom(romPath);
    if (!Port3DS_LoadRussianLocale() && gRomRegion == ROM_REGION_USA) {
        printf("Warning: Russian localization is unavailable; using ROM English text.\n");
    }
    Port_PPU_Init(NULL);
    if (!Port_Audio_Init()) {
        printf("Warning: audio is unavailable.\n");
    }

    printf("Starting engine...\n");
    AgbMain();

    Port_PPU_Shutdown();
    Platform3DS_Shutdown();
    return 0;
}
