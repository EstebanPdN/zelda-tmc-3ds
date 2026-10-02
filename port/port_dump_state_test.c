#include "port_dump_state.h"

#include "region.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int sFailures;

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                             \
        }                                            \
    } while (0)

static SaveFile MakeSave(unsigned area, unsigned room) {
    SaveFile save;

    memset(&save, 0, sizeof(save));
    save.initialized = 1;
    save.msg_speed = 1;
    save.brightness = 1;
    save.stats.health = 24;
    save.stats.maxHealth = 24;
    save.saved_status.area_next = (u8)area;
    save.saved_status.room_next = (u8)room;
    save.saved_status.layer = 1;
    return save;
}

static int MakeDirectory(const char* path) {
    return mkdir(path, 0700) == 0;
}

int main(void) {
    char rootTemplate[] = "/tmp/tmc-dump-state-test-XXXXXX";
    char* root = mkdtemp(rootTemplate);
    char dumps[512];
    char older[512];
    char latest[512];
    char path[512];
    PortDumpStateData loaded;
    PortDumpStateResult result;
    SaveFile olderSave = MakeSave(0x11, 0);
    SaveFile latestSave = MakeSave(0x58, 4);

    CHECK(root != NULL, "temporary root created");
    if (root == NULL)
        return 1;
    snprintf(dumps, sizeof(dumps), "%s/dumps", root);
    snprintf(older, sizeof(older), "%s/dump-20260826-100000", dumps);
    snprintf(latest, sizeof(latest), "%s/dump-20260826-200000", dumps);
    CHECK(MakeDirectory(dumps), "dumps directory created");
    CHECK(MakeDirectory(older), "older dump directory created");
    CHECK(MakeDirectory(latest), "latest dump directory created");

    snprintf(path, sizeof(path), "%s/%s", older, PORT_DUMP_LOAD_STATE_FILENAME);
    CHECK(Port_DumpState_WriteFile(path, TMC_REGION_USA, &olderSave), "older structured state written");
    snprintf(path, sizeof(path), "%s/%s", latest, PORT_DUMP_LOAD_STATE_FILENAME);
    CHECK(Port_DumpState_WriteFile(path, TMC_REGION_USA, &latestSave), "latest structured state written");

    result = Port_DumpState_ReadLatest(dumps, TMC_REGION_USA, &loaded);
    CHECK(result == PORT_DUMP_STATE_OK, "latest structured state loads");
    CHECK(loaded.exactPosition, "structured state reports exact-position support");
    CHECK(loaded.save.saved_status.area_next == 0x58 && loaded.save.saved_status.room_next == 4,
          "lexicographically latest dump wins");
    CHECK(Port_DumpState_ReadLatest(dumps, TMC_REGION_EU, &loaded) == PORT_DUMP_STATE_WRONG_REGION,
          "structured state rejects a different active ROM region");

    {
        FILE* file = fopen(path, "r+b");
        CHECK(file != NULL, "structured state reopened for corruption test");
        if (file != NULL) {
            CHECK(fseek(file, -1, SEEK_END) == 0, "corruption byte selected");
            fputc(0xA5, file);
            fclose(file);
        }
    }
    CHECK(Port_DumpState_ReadLatest(dumps, TMC_REGION_USA, &loaded) == PORT_DUMP_STATE_INVALID,
          "checksum corruption is rejected");

    remove(path);
    snprintf(path, sizeof(path), "%s/%s", latest, PORT_DUMP_LEGACY_STATE_FILENAME);
    {
        FILE* file = fopen(path, "wb");
        CHECK(file != NULL, "legacy state opened");
        if (file != NULL) {
            CHECK(fwrite(&latestSave, 1, sizeof(latestSave), file) == sizeof(latestSave),
                  "legacy state written");
            fclose(file);
        }
    }
    result = Port_DumpState_ReadLatest(dumps, TMC_REGION_USA, &loaded);
    CHECK(result == PORT_DUMP_STATE_OK_LEGACY, "legacy save-state fallback loads");
    CHECK(!loaded.exactPosition, "legacy state is identified as a save checkpoint");

    snprintf(path, sizeof(path), "%s/info.txt", latest);
    {
        FILE* file = fopen(path, "wb");
        CHECK(file != NULL, "legacy region metadata opened");
        if (file != NULL) {
            fputs("ROM region: EU\n", file);
            fclose(file);
        }
    }
    CHECK(Port_DumpState_ReadLatest(dumps, TMC_REGION_USA, &loaded) == PORT_DUMP_STATE_WRONG_REGION,
          "legacy dump metadata rejects a different active ROM region");
    CHECK(Port_DumpState_ReadLatest(dumps, TMC_REGION_EU, &loaded) == PORT_DUMP_STATE_OK_LEGACY,
          "legacy dump metadata accepts its matching ROM region");

    char numbered[512], expected[512];
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(Port_DumpState_CreateDirectoryAt(dumps, "20260911-032524", numbered, sizeof(numbered)), "numbered capture created");
        snprintf(expected, sizeof(expected), "%s/%03u-dump-20260911-032524", dumps, i);
        CHECK(!strcmp(numbered, expected), "sequence is independent of timestamp collisions");
        CHECK(Port_DumpState_ReadLatest(dumps,TMC_REGION_EU,&loaded)==PORT_DUMP_STATE_NO_STATE,
              "incomplete newest dump is not silently skipped");
        snprintf(path,sizeof(path),"%s/%s",numbered,PORT_DUMP_LOAD_STATE_FILENAME);
        CHECK(Port_DumpState_WriteFile(path,TMC_REGION_EU,&latestSave),"numbered state written");
        CHECK(Port_DumpState_ReadLatest(dumps,TMC_REGION_EU,&loaded)==PORT_DUMP_STATE_OK,
              "numbered checkpoint supersedes legacy sessions");
    }
    snprintf(expected,sizeof(expected),"%s/999-dump-20260911-032525",dumps);
    CHECK(MakeDirectory(expected),"999 fixture");
    CHECK(Port_DumpState_CreateDirectoryAt(dumps,"20200101-000000",numbered,sizeof(numbered)),"1000 created after clock rollback");
    snprintf(expected,sizeof(expected),"%s/1000-dump-20200101-000000",dumps);
    CHECK(!strcmp(expected,numbered),"numeric sequence crosses 999");
    CHECK(Port_DumpState_ReadLatest(dumps,TMC_REGION_EU,&loaded)==PORT_DUMP_STATE_NO_STATE,"numeric 1000 is newest");
    CHECK(!Port_DumpState_CreateDirectoryAt(dumps,"../bad",numbered,sizeof(numbered)),"invalid timestamp rejected");
    CHECK(!numbered[0],"failed creation clears output");
    // Remove only the explicitly created test sessions.
    for (unsigned i=0;i<3;++i) {
        snprintf(numbered,sizeof(numbered),"%s/%03u-dump-20260911-032524",dumps,i);
        snprintf(path,sizeof(path),"%s/%s",numbered,PORT_DUMP_LOAD_STATE_FILENAME); remove(path); rmdir(numbered);
    }
    rmdir(expected);
    snprintf(expected,sizeof(expected),"%s/999-dump-20260911-032525",dumps); rmdir(expected);
    snprintf(expected,sizeof(expected),"%s/dump-sequence.txt",dumps); remove(expected);
    snprintf(path, sizeof(path), "%s/info.txt", latest);

    remove(path);
    snprintf(path, sizeof(path), "%s/%s", latest, PORT_DUMP_LEGACY_STATE_FILENAME);
    remove(path);
    snprintf(path, sizeof(path), "%s/%s", older, PORT_DUMP_LOAD_STATE_FILENAME);
    remove(path);
    rmdir(latest);
    rmdir(older);
    rmdir(dumps);
    rmdir(root);

    if (sFailures != 0)
        return 1;
    puts("port_dump_state_test: PASS");
    return 0;
}
