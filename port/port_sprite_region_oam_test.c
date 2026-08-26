#include <stdio.h>
#include <string.h>

#include "global.h"
#include "definitions.h"
#include "enemy.h"
#include "main.h"
#include "object.h"
#include "player.h"
#include "port_config.h"
#include "port_rom.h"
#include "projectile.h"
#include "room.h"
#include "screen.h"
#include "structures.h"
#include "cpu/mode1.h"

/* Production entry points under test (port_draw.c). */
void Port_LoadOverlayDataFromConst(const u8* data, u32 size);
void ram_DrawDirect(OAMCommand* cmd, u32 spriteIndex, u32 frameIndex);
bool32 EnemyInit(Enemy* enemy);
bool32 ProjectileInit(Entity* entity);

/* Minimal runtime owned by RenderSpritePieces. */
u32 gFrameObjLists[50016];
OAMControls gOAMControls;
PlayerState gPlayerState;
PlayerEntity gPlayerEntity;
RomRegion gRomRegion = ROM_REGION_USA;
int gActiveRegion = TMC_REGION_USA;
RoomControls gRoomControls;
u8 virtuappu_mode1_obj_y_negative_staged[MODE1_GBA_OAM_COUNT];
u8 virtuappu_mode1_obj_clip_mark_staged[MODE1_GBA_OAM_COUNT];
int virtuappu_mode1_obj_clip_y_staged;
int virtuappu_mode1_obj_clip_enable_staged;

int Port_Widescreen_EffectiveViewWidth(void) {
    return 240;
}

int Port_Widescreen_EffectiveViewHeight(void) {
    return 160;
}

int Port_Widescreen_ProducerViewWidth(void) {
    return 240;
}

int Port_Widescreen_ProducerViewHeight(void) {
    return 160;
}

u16 Port_RemapSpriteIndex(u16 spriteIndex) {
    return Port_RemapLogicalSpriteIndexForRegion(gRomRegion, spriteIndex);
}

/* Minimal compiled-USA definition tables for exercising the production
 * EnemyInit/ProjectileInit source-conversion sites. */
static const EnemyDefinition kSpearMoblinForms[] = {
    { .spriteIndex = SPRITE_SPEARMOBLIN },
    { .spriteIndex = SPRITE_SPEARMOBLIN_1 },
};
static const EnemyDefinition kBowMoblinForms[] = {
    { .spriteIndex = SPRITE_BOWMOBLIN },
    { .spriteIndex = SPRITE_BOWMOBLIN_1 },
};
EnemyDefinition gEnemyDefinitions[0x70] = {
    [LEEVER] = { .spriteIndex = SPRITE_LEEVER },
    [SPEAR_MOBLIN] = { .gfx = 0xFFFFu, .ptr.definition = kSpearMoblinForms },
    [BOW_MOBLIN] = { .gfx = 0xFFFFu, .ptr.definition = kBowMoblinForms },
    [GYORG_CHILD] = { .spriteIndex = SPRITE_GYORGCHILD },
    [GYORG_FEMALE_EYE] = { .spriteIndex = SPRITE_GYORGFEMALEEYE },
    [GYORG_MALE_EYE] = { .spriteIndex = SPRITE_ENEMY62 },
    [GYORG_FEMALE_MOUTH] = { .spriteIndex = SPRITE_GYORGFEMALEMOUTH },
};
EnemyDefinition gEnemyDefinitions_eu[0x70] = {
    [LEEVER] = { .spriteIndex = SPRITE_LEEVER },
    [SPEAR_MOBLIN] = { .gfx = 0xFFFFu, .ptr.definition = kSpearMoblinForms },
    [BOW_MOBLIN] = { .gfx = 0xFFFFu, .ptr.definition = kBowMoblinForms },
    [GYORG_CHILD] = { .spriteIndex = SPRITE_GYORGCHILD },
    [GYORG_FEMALE_EYE] = { .spriteIndex = SPRITE_GYORGFEMALEEYE },
    [GYORG_MALE_EYE] = { .spriteIndex = SPRITE_ENEMY62 },
    [GYORG_FEMALE_MOUTH] = { .spriteIndex = SPRITE_GYORGFEMALEMOUTH },
};

static const ProjectileDefinition kSpikedRollerForms[] = {
    { .spriteIndex = SPRITE_SPIKEDROLLERS },
};
const ProjectileDefinition gProjectileDefinitions[0x25] = {
    [ARROW_PROJECTILE] = { .spriteIndex = SPRITE_ARROWPROJECTILE },
    [SPIKED_ROLLERS] = { .gfx = 0xFFFFu, .ptr.definition = kSpikedRollerForms },
    [GYORG_MALE_ENERGY_PROJECTILE] = { .spriteIndex = SPRITE_GYORGMALEENERGYPROJECTILE },
};
const ProjectileDefinition gProjectileDefinition_12_alt[] = { { 0 } };
const ProjectileDefinition gProjectileDefinition_25_eu[] = { { 0 } };
const ProjectileDefinition gProjectileDefinition_14_eu[] = { { 0 } };
const ProjectileDefinition gProjectileDefinition_22_eu[] = { { 0 } };

bool32 LoadFixedGFX(Entity* entity, u32 gfx) {
    (void)entity;
    (void)gfx;
    return TRUE;
}

bool32 LoadSwapGFX(Entity* entity, u32 gfx, u32 slot) {
    (void)entity;
    (void)gfx;
    (void)slot;
    return TRUE;
}

u32 LoadObjPalette(Entity* entity, u32 palette) {
    (void)entity;
    (void)palette;
    return 0;
}

void UpdateSpriteForCollisionLayer(Entity* entity) {
    (void)entity;
}

Entity* CreateObject(Object id, u32 type, u32 type2) {
    (void)id;
    (void)type;
    (void)type2;
    return NULL;
}

void CopyPosition(Entity* source, Entity* target) {
    (void)source;
    (void)target;
}

/* Canonical GBA OBJ dimension/anchor table used by the production renderer. */
const u8 kOverlaySizeData[240] = {
    0x00,0x00,0x08,0x08,0x00,0x00,0x10,0x10,0x00,0x00,0x20,0x20,0x00,0x00,0x40,0x40,
    0x00,0x00,0x10,0x08,0x00,0x00,0x20,0x08,0x00,0x00,0x20,0x10,0x00,0x00,0x40,0x20,
    0x00,0x00,0x08,0x10,0x00,0x00,0x08,0x20,0x00,0x00,0x10,0x20,0x00,0x00,0x20,0x40,
    0x08,0x00,0x08,0x08,0x10,0x00,0x10,0x10,0x20,0x00,0x20,0x20,0x40,0x00,0x40,0x40,
    0x10,0x00,0x10,0x08,0x20,0x00,0x20,0x08,0x20,0x00,0x20,0x10,0x40,0x00,0x40,0x20,
    0x08,0x00,0x08,0x10,0x08,0x00,0x08,0x20,0x10,0x00,0x10,0x20,0x20,0x00,0x20,0x40,
    0x00,0x08,0x08,0x08,0x00,0x10,0x10,0x10,0x00,0x20,0x20,0x20,0x00,0x40,0x40,0x40,
    0x00,0x08,0x10,0x08,0x00,0x08,0x20,0x08,0x00,0x10,0x20,0x10,0x00,0x20,0x40,0x20,
    0x00,0x10,0x08,0x10,0x00,0x20,0x08,0x20,0x00,0x20,0x10,0x20,0x00,0x40,0x20,0x40,
    0x08,0x08,0x08,0x08,0x10,0x10,0x10,0x10,0x20,0x20,0x20,0x20,0x40,0x40,0x40,0x40,
    0x10,0x08,0x10,0x08,0x20,0x08,0x20,0x08,0x20,0x10,0x20,0x10,0x40,0x20,0x40,0x20,
    0x08,0x10,0x08,0x10,0x08,0x20,0x08,0x20,0x10,0x20,0x10,0x20,0x20,0x40,0x20,0x40,
    0x04,0x04,0x10,0x10,0x08,0x08,0x20,0x20,0x10,0x10,0x40,0x40,0x20,0x20,0x80,0x80,
    0x08,0x04,0x20,0x10,0x10,0x04,0x40,0x10,0x10,0x08,0x40,0x20,0x20,0x10,0x80,0x40,
    0x04,0x08,0x10,0x20,0x04,0x10,0x10,0x40,0x08,0x10,0x20,0x40,0x10,0x20,0x40,0x80,
};

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                    \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

static void WriteU32(size_t offset, u32 value) {
    u8* base = (u8*)gFrameObjLists;
    base[offset + 0] = (u8)value;
    base[offset + 1] = (u8)(value >> 8);
    base[offset + 2] = (u8)(value >> 16);
    base[offset + 3] = (u8)(value >> 24);
}

static void InstallFrame(u16 nativeIndex, size_t tableOffset, size_t frameOffset, const u8* frame, size_t size) {
    u8* base = (u8*)gFrameObjLists;
    gFrameObjLists[nativeIndex] = (u32)tableOffset;
    WriteU32(tableOffset, (u32)frameOffset);
    memcpy(base + frameOffset, frame, size);
}

static u16 OamHalfword(u32 entry, u32 halfword) {
    u16 value;
    memcpy(&value, (const u8*)&gOAMControls.oam[entry] + halfword * sizeof(u16), sizeof(value));
    return value;
}

static void ClearOam(void) {
    memset(&gOAMControls, 0, sizeof(gOAMControls));
    memset(virtuappu_mode1_obj_y_negative_staged, 0, sizeof(virtuappu_mode1_obj_y_negative_staged));
    memset(virtuappu_mode1_obj_clip_mark_staged, 0, sizeof(virtuappu_mode1_obj_clip_mark_staged));
}

static void CheckProductionSourceConversions(void) {
    Enemy enemy;
    Entity projectile;

    gRomRegion = ROM_REGION_EU;
    gActiveRegion = TMC_REGION_EU;

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = ARROW_PROJECTILE;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "EU arrow projectile initializes");
    CHECK_EQ(projectile.spriteIndex, 320u, "EU arrow stores native sprite 320");
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "EU arrow second init is a no-op");
    CHECK_EQ(projectile.spriteIndex, 320u, "EU arrow is not shifted twice");

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = SPIKED_ROLLERS;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "EU spiked roller initializes");
    CHECK_EQ(projectile.spriteIndex, 301u, "EU spiked roller stores native sprite 301");

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = GYORG_MALE_ENERGY_PROJECTILE;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "EU Gyorg energy projectile initializes");
    CHECK_EQ(projectile.spriteIndex, 305u, "EU Gyorg energy projectile stores native sprite 305");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = SPEAR_MOBLIN;
    enemy.base.type = 1;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU alternate Spear Moblin initializes");
    CHECK_EQ(enemy.base.spriteIndex, 318u, "EU alternate Spear Moblin stores native sprite 318");
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Spear Moblin second init is a no-op");
    CHECK_EQ(enemy.base.spriteIndex, 318u, "EU Spear Moblin is not shifted twice");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = BOW_MOBLIN;
    enemy.base.type = 1;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU alternate Bow Moblin initializes");
    CHECK_EQ(enemy.base.spriteIndex, 319u, "EU alternate Bow Moblin stores native sprite 319");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_CHILD;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Gyorg child initializes");
    CHECK_EQ(enemy.base.spriteIndex, 312u, "EU Gyorg child stores native sprite 312");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_FEMALE_EYE;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Gyorg female eye initializes");
    CHECK_EQ(enemy.base.spriteIndex, 311u, "EU Gyorg female eye stores native sprite 311");
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Gyorg female eye second init is a no-op");
    CHECK_EQ(enemy.base.spriteIndex, 311u, "EU Gyorg female eye is not shifted twice");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_MALE_EYE;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Gyorg male eye initializes");
    CHECK_EQ(enemy.base.spriteIndex, 310u, "EU Gyorg male eye stores native sprite 310");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_FEMALE_MOUTH;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Gyorg female mouth initializes");
    CHECK_EQ(enemy.base.spriteIndex, 314u, "EU Gyorg female mouth stores native sprite 314");

    /* Below-hole ordinary enemies are already native in the regional table
     * and remain untouched even though they share EnemyInit. */
    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = LEEVER;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "EU Leever control initializes");
    CHECK_EQ(enemy.base.spriteIndex, SPRITE_LEEVER, "EU Leever is not remapped");

    gRomRegion = ROM_REGION_USA;
    gActiveRegion = TMC_REGION_USA;

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = ARROW_PROJECTILE;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "USA arrow projectile initializes");
    CHECK_EQ(projectile.spriteIndex, 321u, "USA arrow keeps compiled sprite 321");

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = SPIKED_ROLLERS;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "USA spiked roller initializes");
    CHECK_EQ(projectile.spriteIndex, 302u, "USA spiked roller keeps compiled sprite 302");

    memset(&projectile, 0, sizeof(projectile));
    projectile.id = GYORG_MALE_ENERGY_PROJECTILE;
    CHECK_EQ(ProjectileInit(&projectile), TRUE, "USA Gyorg energy projectile initializes");
    CHECK_EQ(projectile.spriteIndex, 306u, "USA Gyorg energy projectile keeps compiled sprite 306");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = SPEAR_MOBLIN;
    enemy.base.type = 1;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA alternate Spear Moblin initializes");
    CHECK_EQ(enemy.base.spriteIndex, 319u, "USA alternate Spear Moblin keeps sprite 319");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = BOW_MOBLIN;
    enemy.base.type = 1;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA alternate Bow Moblin initializes");
    CHECK_EQ(enemy.base.spriteIndex, 320u, "USA alternate Bow Moblin keeps sprite 320");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_CHILD;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA Gyorg child initializes");
    CHECK_EQ(enemy.base.spriteIndex, 313u, "USA Gyorg child keeps compiled sprite 313");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_FEMALE_EYE;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA Gyorg female eye initializes");
    CHECK_EQ(enemy.base.spriteIndex, 312u, "USA Gyorg female eye keeps compiled sprite 312");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_MALE_EYE;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA Gyorg male eye initializes");
    CHECK_EQ(enemy.base.spriteIndex, 311u, "USA Gyorg male eye keeps compiled sprite 311");

    memset(&enemy, 0, sizeof(enemy));
    enemy.base.id = GYORG_FEMALE_MOUTH;
    CHECK_EQ(EnemyInit(&enemy), TRUE, "USA Gyorg female mouth initializes");
    CHECK_EQ(enemy.base.spriteIndex, 315u, "USA Gyorg female mouth keeps compiled sprite 315");
}

int main(void) {
    /* Deliberately synthetic frame sentinels keep this public regression free
     * of ROM-derived graphics data.  Distinct topologies at adjacent native
     * indices make an omitted/double/absent shift visible in byte-exact OAM:
     *   logical 302 SpikedRollers -> EU native 301 (two pieces)
     *   logical 312 GyorgFemaleEye -> EU native 311 (two pieces)
     *   logical 321 ArrowProjectile -> EU native 320 (one piece)
     * Native 302 is an unrelated one-piece negative control.  Private ROM
     * verification separately proves the retail USA/EU table correspondence. */
    static const u8 kMappedEu301Frame0[] = { 0x02, 0xFD, 0xFA, 0x10, 0x05, 0x00,
                                              0x0D, 0x0A, 0x40, 0x09, 0x00 };
    static const u8 kUnmappedEu302Frame0[] = { 0x01, 0x05, 0xEC, 0x20, 0x0B, 0x00 };
    static const u8 kMappedEu311GyorgEyeFrame0[] = { 0x02, 0xFA, 0xFB, 0x00, 0x0C, 0x00,
                                                     0x08, 0x05, 0x10, 0x10, 0x00 };
    static const u8 kWrongEu312GyorgChildFrame0[] = { 0x04, 0xF8, 0xFA, 0x41, 0x10, 0x40,
                                                       0xFC, 0x08, 0x01, 0x14, 0x20,
                                                       0x00, 0xF8, 0x15, 0x00, 0x20,
                                                       0xF0, 0xF8, 0x11, 0x00, 0x20 };
    static const u8 kMappedEu320Frame0[] = { 0x01, 0xF9, 0x04, 0x40, 0x0D, 0x00 };
    OAMCommand cmd;

    memset(gFrameObjLists, 0, sizeof(gFrameObjLists));
    InstallFrame(301u, 0x1000u, 0x1100u, kMappedEu301Frame0, sizeof(kMappedEu301Frame0));
    InstallFrame(302u, 0x1010u, 0x1110u, kUnmappedEu302Frame0, sizeof(kUnmappedEu302Frame0));
    InstallFrame(320u, 0x1020u, 0x1120u, kMappedEu320Frame0, sizeof(kMappedEu320Frame0));
    InstallFrame(311u, 0x1030u, 0x1130u, kMappedEu311GyorgEyeFrame0, sizeof(kMappedEu311GyorgEyeFrame0));
    InstallFrame(312u, 0x1040u, 0x1140u, kWrongEu312GyorgChildFrame0, sizeof(kWrongEu312GyorgChildFrame0));
    Port_LoadOverlayDataFromConst(kOverlaySizeData, sizeof(kOverlaySizeData));
    CheckProductionSourceConversions();

    memset(&cmd, 0, sizeof(cmd));
    cmd.x = 41;
    cmd.y = 144;
    cmd._8 = 0x8750;
    gRomRegion = ROM_REGION_EU;
    CHECK_EQ(Port_RemapSpriteIndex(302u), 301u, "EU roller source is converted once at entity init");
    ClearOam();
    ram_DrawDirect(&cmd, Port_RemapSpriteIndex(302u), 0u);
    CHECK_EQ(gOAMControls.updated, 2u, "EU spiked roller selects the mapped two-piece sentinel");
    CHECK_EQ(OamHalfword(0, 0), 0x008Au, "EU spiked roller piece 0 attr0");
    CHECK_EQ(OamHalfword(0, 1), 0x4026u, "EU spiked roller piece 0 attr1");
    CHECK_EQ(OamHalfword(0, 2), 0x8755u, "EU spiked roller piece 0 attr2");
    CHECK_EQ(OamHalfword(1, 0), 0x409Au, "EU spiked roller piece 1 attr0");
    CHECK_EQ(OamHalfword(1, 1), 0x0036u, "EU spiked roller piece 1 attr1");
    CHECK_EQ(OamHalfword(1, 2), 0x8759u, "EU spiked roller piece 1 attr2");

    /* The renderer consumes native indices and must not apply a hidden second
     * shift. Passing native EU 301 directly therefore produces the identical
     * byte-exact OAM result. */
    ClearOam();
    ram_DrawDirect(&cmd, 301u, 0u);
    CHECK_EQ(gOAMControls.updated, 2u, "native renderer input is not double-remapped");
    CHECK_EQ(OamHalfword(0, 0), 0x008Au, "native roller path piece 0 attr0");
    CHECK_EQ(OamHalfword(1, 0), 0x409Au, "native roller path piece 1 attr0");

    /* Negative control: without the one-time source conversion, native 302
     * is the adjacent one-piece sentinel rather than the mapped topology. */
    ClearOam();
    ram_DrawDirect(&cmd, 302u, 0u);
    CHECK_EQ(gOAMControls.updated, 1u, "unshifted trap negative control emits unrelated one-piece frame");
    CHECK_EQ(OamHalfword(0, 0), 0x007Cu, "unshifted trap negative-control attr0");
    CHECK_EQ(OamHalfword(0, 1), 0x802Eu, "unshifted trap negative-control attr1");

    {
        Enemy eye;
        memset(&eye, 0, sizeof(eye));
        eye.base.id = GYORG_FEMALE_EYE;
        CHECK_EQ(EnemyInit(&eye), TRUE, "EU Gyorg eye initializes for OAM regression");
        CHECK_EQ(eye.base.spriteIndex, 311u, "EU Gyorg eye selects its native frame-list index");

        memset(&cmd, 0, sizeof(cmd));
        cmd.x = 100;
        cmd.y = 40;
        cmd._8 = 0x2A60;
        ClearOam();
        ram_DrawDirect(&cmd, eye.base.spriteIndex, 0u);
        CHECK_EQ(gOAMControls.updated, 2u, "EU Gyorg eye emits the intended two-piece frame");

        ClearOam();
        ram_DrawDirect(&cmd, SPRITE_GYORGFEMALEEYE, 0u);
        CHECK_EQ(gOAMControls.updated, 4u,
                 "unshifted EU Gyorg eye negative control emits the adjacent child topology");
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.x = 120;
    cmd.y = 59;
    cmd._8 = 0x6960;
    gRomRegion = ROM_REGION_EU;
    CHECK_EQ(Port_RemapSpriteIndex(321u), 320u, "EU arrow source is converted once at entity init");
    ClearOam();
    ram_DrawDirect(&cmd, Port_RemapSpriteIndex(321u), 0u);
    CHECK_EQ(gOAMControls.updated, 1u, "EU arrow selects the mapped one-piece sentinel");
    CHECK_EQ(OamHalfword(0, 0), 0x403Fu, "EU arrow sentinel attr0");
    CHECK_EQ(OamHalfword(0, 1), 0x0071u, "EU arrow sentinel attr1");
    CHECK_EQ(OamHalfword(0, 2), 0x696Du, "EU arrow sentinel attr2 keeps projectile tile/palette/priority");

    gRomRegion = ROM_REGION_EU;
    ClearOam();
    ram_DrawDirect(&cmd, Port_RemapSpriteIndex(PORT_EU_OMITTED_SPRITE_INDEX), 0u);
    CHECK_EQ(gOAMControls.updated, 0u, "EU USA-only sprite hole fails closed without aliasing geometry");

    /* A pointer into the 484-byte USA-only tail must be rejected for EU even
     * though the host buffer has spare capacity there. */
    memset(gFrameObjLists, 0, sizeof(gFrameObjLists));
    gFrameObjLists[1] = PORT_EU_FRAME_OBJ_LISTS_SIZE;
    ClearOam();
    ram_DrawDirect(&cmd, 1u, 0u);
    CHECK_EQ(gOAMControls.updated, 0u, "EU renderer rejects frame data in the unloaded USA-only tail");

    if (sFailures != 0) {
        fprintf(stderr, "port_sprite_region_oam_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_sprite_region_oam_test: ALL PASS");
    return 0;
}
