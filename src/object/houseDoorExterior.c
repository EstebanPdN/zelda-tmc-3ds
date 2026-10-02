/**
 * @file houseDoorExterior.c
 * @ingroup Objects
 *
 * @brief House Door Exterior object
 */
#include "entity.h"
#include "flags.h"
#include "main.h"
#include "npc.h"
#include "object.h"
#include "asm.h"
#include "player.h"
#include "room.h"
#include "script.h"
#include "sound.h"
#ifdef PC_PORT
#include "port_rom.h"
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u16 unk_68;
    /*0x6a*/ u16 unk_6a;
    /*0x6c*/ u8 unk_6c;
    /*0x6d*/ u8 unused1[23];
    /*0x84*/ ScriptExecutionContext* context;
} HouseDoorExteriorEntity;

typedef struct {
    /*0x00*/ u16 unk0;
    /*0x02*/ u16 unk2;
    /*0x04*/ u8 unk4;
    /*0x05*/ u8 unk5;
    /*0x06*/ u8 unk6;
    /*0x07*/ u8 unk7;
    /*0x08*/ u8* unk8;
} unk_DoorProperty;

static void sub_080868EC(Entity* entity, ScriptExecutionContext* context);
static bool32 sub_080867CC(u32);
void HouseDoorExterior_Type2(HouseDoorExteriorEntity* this);
void HouseDoorExterior_Type0(HouseDoorExteriorEntity* this);
void HouseDoorExterior_Type1(HouseDoorExteriorEntity* this);
void HouseDoorExterior_Type3(HouseDoorExteriorEntity* this);
static u8 sub_08086954(HouseDoorExteriorEntity* this);

static const Hitbox gUnk_081206AC = { 0, -5, { 5, 3, 3, 5 }, 10, 4 };

void HouseDoorExterior(HouseDoorExteriorEntity* this) {
    static void (*const HouseDoorExterior_Types[])(HouseDoorExteriorEntity*) = {
        HouseDoorExterior_Type0,
        HouseDoorExterior_Type1,
        HouseDoorExterior_Type2,
        HouseDoorExterior_Type3,
    };
    /* Defensive: a child door entity can get spawned with type2 > 3 when
     * the parent's room-property iteration reads garbage bytes (see #29,
     * #30 — bridge/Trilby random crashes). The dispatch would run off the
     * end of the table and call a random function pointer. Skip the
     * dispatch if out of range so the bad child stays inert instead of
     * crashing. The root cause (wrong room-property index) needs separate
     * investigation. */
    if (super->type2 >= (sizeof(HouseDoorExterior_Types) / sizeof(HouseDoorExterior_Types[0]))) {
        return;
    }
    HouseDoorExterior_Types[super->type2](this);
}

void HouseDoorExterior_Type0(HouseDoorExteriorEntity* this) {
    HouseDoorExteriorEntity* entity;
    u32 i;

    if (super->action == 0) {
        super->action = 1;
        *((u32*)(&this->unk_68)) = 0;
        this->unk_6c = super->timer;
        SetEntityPriority(super, PRIO_PLAYER_EVENT);
    }

    if (this->unk_6c < 8) {
        return;
    }

#ifdef PC_PORT
    /* Property indices 0..7 are reserved for the standard room
     * functions/lists (entities, tile entities, room init/update, etc.).
     * Door property data lives at index 8+ (additional). If a parent
     * door's super->timer somehow lands at a reserved index — usually
     * because EntityData read the wrong byte for type2's high half — we'd
     * read e.g. the entity list as 12-byte door property records and
     * spawn dozens of garbage children (#28, #29, #30 root cause).
     * Bail out instead. */
    if (this->unk_6c < 8) {
        return;
    }
    {
        const u8* raw = (const u8*)GetCurrentRoomProperty(this->unk_6c);
        if (!raw)
            return;
        for (i = 0; i < 32; i++, raw += 12) {
            u16 p_unk0 = Port_ReadU16(raw + 0);
            if (p_unk0 == 0xFFFF)
                break;
            u16 p_unk2 = Port_ReadU16(raw + 2);
            u8 p_unk4 = raw[4];
            u8 p_unk5 = raw[5];
            u8 p_unk6 = raw[6];
            u8 p_unk7 = raw[7];
            u32 p_unk8 = Port_ReadU32(raw + 8); /* GBA script pointer (4 bytes) */
            int mask = 1 << i;
            if ((*((u32*)(&this->unk_68)) & mask) == 0 && sub_080867CC(p_unk5) &&
                CheckRegionOnScreen(p_unk0, p_unk2, 32, 32)) {
                entity = (HouseDoorExteriorEntity*)CreateObject(HOUSE_DOOR_EXT, p_unk7, p_unk6);
                if (entity != NULL) {
                    entity->unk_6c = i;
                    entity->base.x.HALF.HI = gRoomControls.origin_x + p_unk0 + 16;
                    entity->base.y.HALF.HI = gRoomControls.origin_y + p_unk2 + 32;
                    entity->base.parent = super;
                    entity->unk_68 = p_unk0;
                    entity->unk_6a = p_unk2;
                    entity->base.collisionLayer = p_unk4;
                    entity->base.subAction = p_unk5;
                    UpdateSpriteForCollisionLayer(&entity->base);
                    *((u32*)(&this->unk_68)) |= mask;
                    if (p_unk8) {
                        u16* script = (u16*)Port_ResolveRomData(p_unk8);
                        if (script)
                            entity->context = StartCutscene(&entity->base, script);
                    }
                }
            }
        }
    }
#else
    {
        unk_DoorProperty* prop;
        prop = GetCurrentRoomProperty(this->unk_6c);
        for (i = 0; prop->unk0 != 0xFFFF && i < 32; prop++, i++) {
            int mask = 1 << i;
            if ((*((u32*)(&this->unk_68)) & mask) == 0 && sub_080867CC(prop->unk5) &&
                CheckRegionOnScreen(prop->unk0, prop->unk2, 32, 32)) {
                entity = (HouseDoorExteriorEntity*)CreateObject(HOUSE_DOOR_EXT, prop->unk7, prop->unk6);
                if (entity != NULL) {
                    entity->unk_6c = i;
                    entity->base.x.HALF.HI = gRoomControls.origin_x + prop->unk0 + 16;
                    entity->base.y.HALF.HI = gRoomControls.origin_y + prop->unk2 + 32;
                    entity->base.parent = super;
                    entity->unk_68 = prop->unk0;
                    entity->unk_6a = prop->unk2;
                    entity->base.collisionLayer = prop->unk4;
                    entity->base.subAction = prop->unk5;
                    UpdateSpriteForCollisionLayer(&entity->base);
                    *((u32*)(&this->unk_68)) |= mask;
                    if (prop->unk8) {
                        entity->context = StartCutscene(&entity->base, (u16*)prop->unk8);
                    }
                }
            }
        }
    }
#endif
}

static bool32 sub_080867CC(u32 arg0) {
    if (arg0 == 0) {
        return TRUE;
    }
    if (arg0 != 2) {
        return TRUE;
    }
    return CheckGlobalFlag(TATEKAKE_HOUSE);
}

void HouseDoorExterior_Type1(HouseDoorExteriorEntity* this) {
    if (!CheckRegionOnScreen(this->unk_68, this->unk_6a, 32, 32)) {
        *((u32*)(&((HouseDoorExteriorEntity*)super->parent)->unk_68)) =
            *((u32*)(&((HouseDoorExteriorEntity*)super->parent)->unk_68)) & ~(1 << this->unk_6c);
        DeleteThisEntity();
    }
    HouseDoorExterior_Type2(this);
}

void HouseDoorExterior_Type2(HouseDoorExteriorEntity* this) {
    switch (super->action) {
        case 0:
            super->action = 1;
            super->timer = 8;
            super->spriteSettings.draw = 1;
            super->frameIndex = 0;
            super->hitbox = (Hitbox*)&gUnk_081206AC;
            if (super->subAction == 1) {
                super->action = 2;
                super->frameIndex = 1;
            }
            if (super->flags & ENT_SCRIPTED) {
                super->action = 2;
            }
            break;
        case 1:
            if (!sub_08086954(this)) {
                super->action++;
                super->frameIndex = 1;
                sub_08078AC0(16, 0, 1);
                SoundReq(SFX_111);
            }
            break;
    }

    if (super->flags & ENT_SCRIPTED) {
#ifdef PC_PORT
        if (this->context != NULL) {
#endif
        ExecuteScript(super, this->context);
        sub_080868EC(super, this->context);
#ifdef PC_PORT
        }
#endif
    }
}

void HouseDoorExterior_Type3(HouseDoorExteriorEntity* this) {
    if (super->action == 0) {
        super->action = 1;
        super->spriteSettings.draw = 1;
        super->hitbox = (Hitbox*)&gUnk_081206AC;
        super->timer = 8;
    }
#ifdef PC_PORT
    /* On the port, Port_ResolveRomData can return NULL for door scripts whose
     * GBA address isn't backed by the loaded ROM (or whose asset is missing),
     * leaving entity->context unset by the spawner. The GBA original always
     * had valid pointers; here we skip the script path instead of crashing. */
    if (this->context == NULL) {
        return;
    }
#endif
    ExecuteScript(super, this->context);
    sub_080868EC(super, this->context);
}

static void sub_080868EC(Entity* entity, ScriptExecutionContext* context) {
    u32 postScriptActions = context->postScriptActions;
    context->postScriptActions = 0;
    while (postScriptActions != 0) {
        u32 rightMostSetBit = postScriptActions & (~postScriptActions + 1);
        postScriptActions ^= rightMostSetBit;
        switch (rightMostSetBit) {
            case 0x80:
                entity->frameIndex = 0;
                break;
            case 0x100:
                entity->frameIndex = 1;
                break;
        }
    }

    if (entity->frameIndex == 0) {
        sub_0800445C(entity);
    }
}

void sub_0808692C(HouseDoorExteriorEntity* this) {
    super->flags &= ~ENT_SCRIPTED;
    super->type2 = 2;
    super->action = (super->frameIndex == 0) ? 1 : 2;
    super->subAction = 0;
    super->timer = 8;
}

static u8 sub_08086954(HouseDoorExteriorEntity* this) {
    if (sub_0800445C(super)) {
        if (GetAnimationStateInRectRadius(super, 6, 20) >= 0 && gPlayerEntity.base.animationState == 0 &&
            (u16)gPlayerState.playerInput.heldInput == INPUT_UP && gPlayerState.jump_status == 0) {
            super->timer--;
        }
    } else {
        super->timer = 8;
    }
    return super->timer;
}

void sub_080869A4(HouseDoorExteriorEntity* this, ScriptExecutionContext* context) {
    context->condition = 0;
    if (!sub_08086954(this)) {
        super->timer = 8;
        context->condition = 1;
    }
}
