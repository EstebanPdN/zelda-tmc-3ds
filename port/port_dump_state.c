#include "port_dump_state.h"

#include "area.h"
#include "main.h"
#include "region.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PORT_DUMP_STATE_VERSION 1u
#define PORT_DUMP_STATE_MAX_PATH 512

typedef struct PortDumpStateFileV1 {
    uint8_t magic[8];
    uint32_t version;
    uint32_t region;
    uint32_t payloadSize;
    uint32_t payloadChecksum;
    SaveFile save;
} PortDumpStateFileV1;

static const uint8_t sLoadStateMagic[8] = { 'T', 'M', 'C', '3', 'L', 'D', 'S', '1' };

static uint32_t PayloadChecksum(const void* data, size_t size) {
    const uint8_t* bytes = data;
    uint32_t hash = 2166136261u;

    while (size-- != 0) {
        hash ^= *bytes++;
        hash *= 16777619u;
    }
    return hash;
}

static bool SaveLooksValid(const SaveFile* save) {
    return save != NULL && save->invalid == 0 && save->initialized == 1 && save->msg_speed <= MAX_MSG_SPEED &&
           save->brightness <= MAX_BRIGHTNESS && save->saved_status.area_next <= AREA_98 &&
           save->saved_status.room_next < MAX_ROOMS;
}

static bool ReadExactFile(const char* path, void* data, size_t size) {
    FILE* file;
    bool ok;

    errno = 0;
    file = fopen(path, "rb");
    if (file == NULL)
        return false;
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok;
}

bool Port_DumpState_WriteFile(const char* path, int region, const SaveFile* save) {
    PortDumpStateFileV1 state;
    FILE* file;
    bool ok;

    if (path == NULL || !SaveLooksValid(save) || region < TMC_REGION_USA || region > TMC_REGION_JP)
        return false;

    memset(&state, 0, sizeof(state));
    memcpy(state.magic, sLoadStateMagic, sizeof(state.magic));
    state.version = PORT_DUMP_STATE_VERSION;
    state.region = (uint32_t)region;
    state.payloadSize = sizeof(state.save);
    memcpy(&state.save, save, sizeof(state.save));
    state.payloadChecksum = PayloadChecksum(&state.save, sizeof(state.save));

    file = fopen(path, "wb");
    if (file == NULL)
        return false;
    ok = fwrite(&state, 1, sizeof(state), file) == sizeof(state) && fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static PortDumpStateResult ReadStructuredState(const char* path, int activeRegion, PortDumpStateData* out) {
    PortDumpStateFileV1 state;

    if (!ReadExactFile(path, &state, sizeof(state)))
        return errno == ENOENT ? PORT_DUMP_STATE_NO_STATE : PORT_DUMP_STATE_INVALID;
    if (memcmp(state.magic, sLoadStateMagic, sizeof(state.magic)) != 0 ||
        state.version != PORT_DUMP_STATE_VERSION || state.payloadSize != sizeof(state.save) ||
        PayloadChecksum(&state.save, sizeof(state.save)) != state.payloadChecksum || !SaveLooksValid(&state.save)) {
        return PORT_DUMP_STATE_INVALID;
    }
    if (state.region != (uint32_t)activeRegion)
        return PORT_DUMP_STATE_WRONG_REGION;

    memcpy(&out->save, &state.save, sizeof(out->save));
    out->exactPosition = true;
    return PORT_DUMP_STATE_OK;
}

static int ReadLegacyRegion(const char* dumpDirectory) {
    char path[PORT_DUMP_STATE_MAX_PATH];
    char line[160];
    FILE* file;
    int region = -1;

    if (snprintf(path, sizeof(path), "%s/info.txt", dumpDirectory) >= (int)sizeof(path))
        return -1;
    file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "ROM region: EU") != NULL) {
            region = TMC_REGION_EU;
            break;
        }
        if (strstr(line, "ROM region: USA") != NULL) {
            region = TMC_REGION_USA;
            break;
        }
        if (strstr(line, "ROM region: JP") != NULL) {
            region = TMC_REGION_JP;
            break;
        }
    }
    fclose(file);
    return region;
}

static PortDumpStateResult ReadLegacyState(const char* dumpDirectory, int activeRegion, PortDumpStateData* out) {
    char path[PORT_DUMP_STATE_MAX_PATH];
    int region = ReadLegacyRegion(dumpDirectory);

    if (region >= 0 && region != activeRegion)
        return PORT_DUMP_STATE_WRONG_REGION;
    if (snprintf(path, sizeof(path), "%s/%s", dumpDirectory, PORT_DUMP_LEGACY_STATE_FILENAME) >=
        (int)sizeof(path)) {
        return PORT_DUMP_STATE_IO_ERROR;
    }
    if (!ReadExactFile(path, &out->save, sizeof(out->save)))
        return errno == ENOENT ? PORT_DUMP_STATE_NO_STATE : PORT_DUMP_STATE_INVALID;
    if (!SaveLooksValid(&out->save))
        return PORT_DUMP_STATE_INVALID;
    out->exactPosition = false;
    return PORT_DUMP_STATE_OK_LEGACY;
}

static PortDumpStateResult FindLatestDump(const char* dumpsDirectory, char* out, size_t outSize) {
    DIR* directory;
    struct dirent* entry;
    char latest[256] = "";

    directory = opendir(dumpsDirectory);
    if (directory == NULL)
        return errno == ENOENT ? PORT_DUMP_STATE_NO_DUMP : PORT_DUMP_STATE_IO_ERROR;
    while ((entry = readdir(directory)) != NULL) {
        char path[PORT_DUMP_STATE_MAX_PATH];
        struct stat info;

        if (strncmp(entry->d_name, "dump-", 5) != 0)
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dumpsDirectory, entry->d_name) >= (int)sizeof(path))
            continue;
        if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode))
            continue;
        if (latest[0] == '\0' || strcmp(entry->d_name, latest) > 0)
            snprintf(latest, sizeof(latest), "%s", entry->d_name);
    }
    closedir(directory);

    if (latest[0] == '\0')
        return PORT_DUMP_STATE_NO_DUMP;
    if (snprintf(out, outSize, "%s/%s", dumpsDirectory, latest) >= (int)outSize)
        return PORT_DUMP_STATE_IO_ERROR;
    return PORT_DUMP_STATE_OK;
}

PortDumpStateResult Port_DumpState_ReadLatest(const char* dumpsDirectory, int activeRegion,
                                              PortDumpStateData* out) {
    char dumpDirectory[PORT_DUMP_STATE_MAX_PATH];
    char path[PORT_DUMP_STATE_MAX_PATH];
    PortDumpStateResult result;

    if (dumpsDirectory == NULL || out == NULL || activeRegion < TMC_REGION_USA || activeRegion > TMC_REGION_JP)
        return PORT_DUMP_STATE_INVALID;
    memset(out, 0, sizeof(*out));
    result = FindLatestDump(dumpsDirectory, dumpDirectory, sizeof(dumpDirectory));
    if (result != PORT_DUMP_STATE_OK)
        return result;
    if (snprintf(path, sizeof(path), "%s/%s", dumpDirectory, PORT_DUMP_LOAD_STATE_FILENAME) >= (int)sizeof(path))
        return PORT_DUMP_STATE_IO_ERROR;

    result = ReadStructuredState(path, activeRegion, out);
    if (result == PORT_DUMP_STATE_NO_STATE)
        result = ReadLegacyState(dumpDirectory, activeRegion, out);
    return result;
}

const char* Port_DumpState_ResultLabel(PortDumpStateResult result) {
    switch (result) {
        case PORT_DUMP_STATE_OK: return "LOADED";
        case PORT_DUMP_STATE_OK_LEGACY: return "LEGACY";
        case PORT_DUMP_STATE_NO_DUMP: return "NO DUMP";
        case PORT_DUMP_STATE_NO_STATE: return "NO STATE";
        case PORT_DUMP_STATE_INVALID: return "INVALID";
        case PORT_DUMP_STATE_WRONG_REGION: return "WRONG ROM";
        case PORT_DUMP_STATE_IO_ERROR: return "I O ERROR";
    }
    return "ERROR";
}
