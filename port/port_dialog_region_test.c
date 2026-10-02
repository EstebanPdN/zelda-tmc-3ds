#include <stdio.h>
#include <string.h>

#include "common.h"
#include "flags.h"
#include "message.h"
#include "npc.h"
#include "player.h"

PlayerState gPlayerState;

static int sFailures;
static bool32 sPlainLocalState;
static bool32 sBaselineLocalState;
static bool32 sGlobalState;
static u32 sPlainFlag;
static u32 sBaselineFlag;
static u32 sMessage;
static u32 sPlainChecks;
static u32 sBaselineChecks;
static u32 sPlainWrites;
static u32 sBaselineWrites;

#define CHECK(condition, description)                                                  \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "FAIL: %s (line %d)\n", (description), __LINE__);       \
            ++sFailures;                                                               \
        }                                                                              \
    } while (0)

static void ResetState(void) {
    sPlainLocalState = FALSE;
    sBaselineLocalState = FALSE;
    sGlobalState = FALSE;
    sPlainFlag = 0;
    sBaselineFlag = 0;
    sMessage = 0;
    sPlainChecks = 0;
    sBaselineChecks = 0;
    sPlainWrites = 0;
    sBaselineWrites = 0;
}

bool32 CheckLocalFlag(u32 flag) {
    sPlainFlag = flag;
    ++sPlainChecks;
    return sPlainLocalState;
}

void SetLocalFlag(u32 flag) {
    sPlainFlag = flag;
    ++sPlainWrites;
    sPlainLocalState = TRUE;
}

void ClearLocalFlag(u32 flag) {
    sPlainFlag = flag;
    ++sPlainWrites;
    sPlainLocalState = FALSE;
}

bool32 CheckLocalFlagB(u32 flag) {
    sBaselineFlag = flag;
    ++sBaselineChecks;
    return sBaselineLocalState;
}

void SetLocalFlagB(u32 flag) {
    sBaselineFlag = flag;
    ++sBaselineWrites;
    sBaselineLocalState = TRUE;
}

void ClearLocalFlagB(u32 flag) {
    sBaselineFlag = flag;
    ++sBaselineWrites;
    sBaselineLocalState = FALSE;
}

bool32 CheckRoomFlag(u32 flag) {
    (void)flag;
    return FALSE;
}

void SetRoomFlag(u32 flag) {
    (void)flag;
}

void ClearRoomFlag(u32 flag) {
    (void)flag;
}

bool32 CheckGlobalFlag(u32 flag) {
    (void)flag;
    return sGlobalState;
}

void SetGlobalFlag(u32 flag) {
    (void)flag;
    sGlobalState = TRUE;
}

void ClearGlobalFlag(u32 flag) {
    (void)flag;
    sGlobalState = FALSE;
}

u32 CheckKinstoneFused(u32 kinstoneId) {
    (void)kinstoneId;
    return FALSE;
}

u32 GetInventoryValue(u32 item) {
    (void)item;
    return 0;
}

void MessageNoOverlap(u32 index, Entity* entity) {
    (void)entity;
    sMessage = index;
}

void MessageFromTarget(u32 index) {
    sMessage = index;
}

static void RunSetTests(void) {
    Entity entity;
    const Dialog dialog = { KUMOUE_GIRL_TALK, DIALOG_LOCAL_FLAG, DIALOG_SET_FLAG, 1, { 0x111, 0x222 } };

    memset(&entity, 0, sizeof(entity));
    ResetState();
    ShowNPCDialogueB(&entity, &dialog);
    CHECK(sBaselineChecks == 1 && sBaselineWrites == 1, "baseline Dialog uses baseline set helpers");
    CHECK(sPlainChecks == 0 && sPlainWrites == 0, "baseline Dialog never touches region-native helpers");
    CHECK(sBaselineFlag == KUMOUE_GIRL_TALK, "baseline Dialog preserves semantic ordinal provenance");
    CHECK(sMessage == 0x222, "first baseline set displays unseen text");

    ShowNPCDialogueB(&entity, &dialog);
    CHECK(sMessage == 0x111, "repeated baseline set displays seen text");

    ResetState();
    ShowNPCDialogue(&entity, &dialog);
    CHECK(sPlainChecks == 1 && sPlainWrites == 1, "ROM-native Dialog retains plain set helpers");
    CHECK(sBaselineChecks == 0 && sBaselineWrites == 0, "ROM-native Dialog is never remapped twice");
}

static void RunToggleAndCheckTests(void) {
    Entity entity;
    const Dialog toggle = { MACHI_MES_20, DIALOG_LOCAL_FLAG, DIALOG_TOGGLE_FLAG, 1, { 0x333, 0x444 } };
    const Dialog check = { MACHI_07_BELL, DIALOG_LOCAL_FLAG, DIALOG_CHECK_FLAG, 1, { 0x555, 0x666 } };

    memset(&entity, 0, sizeof(entity));
    ResetState();
    ShowNPCDialogueB(&entity, &toggle);
    CHECK(sBaselineLocalState && sMessage == 0x444, "baseline toggle sets and displays false branch");
    ShowNPCDialogueB(&entity, &toggle);
    CHECK(!sBaselineLocalState && sMessage == 0x333, "baseline toggle clears and displays true branch");

    ResetState();
    ShowNPCDialogueB(&entity, &check);
    CHECK(sMessage == 0x666 && sBaselineWrites == 0, "baseline check false branch never writes");
    sBaselineLocalState = TRUE;
    ShowNPCDialogueB(&entity, &check);
    CHECK(sMessage == 0x555 && sBaselineWrites == 0, "baseline check true branch never writes");
}

int main(void) {
    RunSetTests();
    RunToggleAndCheckTests();
    if (sFailures != 0) {
        fprintf(stderr, "port_dialog_region_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_dialog_region_test: ALL PASS");
    return 0;
}
