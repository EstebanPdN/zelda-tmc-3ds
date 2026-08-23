#include "global.h"
#include "room.h"
#include "flags.h"
#include "area.h"
#include "save.h"

const u16 gLocalFlagBanks[] = {
    FLAG_BANK_G, FLAG_BANK_0, FLAG_BANK_1, FLAG_BANK_2, FLAG_BANK_3,  FLAG_BANK_4,  FLAG_BANK_5,
    FLAG_BANK_6, FLAG_BANK_7, FLAG_BANK_8, FLAG_BANK_9, FLAG_BANK_10, FLAG_BANK_11, FLAG_BANK_12,
};

u32 CheckLocalFlag(u32 flag) {
    return CheckLocalFlagByBank(gArea.localFlagOffset, flag);
}

u32 CheckFlags(u32 flags) {
    u32 type;
    u32 index;
    u32 length;
    index = flags & 0x3ff;
    length = (((flags & (0xf0) << 0x6) >> 0xa) + 1);
    type = (flags & 0xc000) >> 0xe;
    switch (type) {
        case 2:
            return CheckRoomFlags(index, length);
        case 0:
            return CheckLocalFlags(index, length);
        case 1:
            return CheckGlobalFlags(index, length);
        default:
            return 0;
    }
}

u32 CheckGlobalFlag(u32 flag) {
    return CheckLocalFlagByBank(FLAG_BANK_0, flag);
}

u32 CheckRoomFlag(u32 flag) {
    return ReadBit(&gRoomVars.flags, flag);
}

u32 CheckLocalFlagsByBank(u32 offset, u32 flag, u32 count) {
    return CheckBits(gSave.flags, offset + flag, count);
}

u32 CheckLocalFlags(u32 flag, u32 count) {
    return CheckLocalFlagsByBank(gArea.localFlagOffset, flag, count);
}

u32 CheckGlobalFlags(u32 flag, u32 count) {
    return CheckLocalFlagsByBank(FLAG_BANK_0, flag, count);
}

u32 CheckRoomFlags(u32 flag, u32 count) {
    return CheckBits(&gRoomVars.flags, flag, count);
}

void SetLocalFlagByBank(u32 offset, u32 flag) {
    if (flag != 0) {
        WriteBit(gSave.flags, offset + flag);
    }
}

void SetLocalFlag(u32 flag) {
    SetLocalFlagByBank(gArea.localFlagOffset, flag);
}

void SetFlag(u32 flag) {
    u32 type;
    u32 index;

    if (flag != 0) {
        index = flag & 0x3ff;
        type = (flag & 0xc000) >> 0xe;
        switch (type) {
            case 2:
                SetRoomFlag(index);
                return;
            case 0:
                SetLocalFlag(index);
                return;
            case 1:
                SetGlobalFlag(index);
                return;
        }
    }
}

void SetGlobalFlag(u32 flag) {
    SetLocalFlagByBank(FLAG_BANK_0, flag);
}

void SetRoomFlag(u32 flag) {
    WriteBit(&gRoomVars.flags, flag);
}

void ClearLocalFlagByBank(u32 offset, u32 flag) {
    ClearBit(gSave.flags, offset + flag);
}

void ClearLocalFlag(u32 flag) {
    ClearLocalFlagByBank(gArea.localFlagOffset, flag);
}

void ClearFlag(u32 flag) {
    u32 type;
    u32 index;

    index = flag & 0x3ff;
    type = (flag & 0xc000) >> 0xe;
    switch (type) {
        case 2:
            ClearRoomFlag(index);
            return;
        case 0:
            ClearLocalFlag(index);
            return;
        case 1:
            ClearGlobalFlag(index);
            return;
    }
}

void ClearGlobalFlag(u32 flag) {
    ClearLocalFlagByBank(FLAG_BANK_0, flag);
}

void ClearRoomFlag(u32 flag) {
    ClearBit(&gRoomVars.flags, flag);
}

#if defined(PC_PORT) && defined(MULTI_REGION)
#include "region.h"
#include "flag_remap_generated.h"

/*
 * Per-region local-flag ordinal remap (M4 flags.h divergence fix).
 *
 * The save-flag banks LocalFlags1..12 are ordered differently per region, so a
 * logical flag's bit (= bankBase + ordinal) differs between USA and EU/JP. The
 * fat binary compiles flags.h with USA-baseline ordinals, but area/room/script
 * data loaded from an EU/JP ROM references flags by that region's ordinals
 * (port_rom.c Port_Resolve*FromRom). ROM-sourced references are therefore
 * already region-correct and untouched; only C references proven to have USA
 * provenance (named USA enums and compiled USA const-table fields) are remapped
 * via the *B helpers below. A USA-only name is invalid in a target region even
 * when an unrelated target flag happens to have the same numeric ordinal.
 */
static int LocalBankNumberForOffset(u32 offset) {
    int i;
    /* gLocalFlagBanks = {G,0,1,2,...,12}; entries [2..13] are FLAG_BANK_1..12. */
    for (i = 2; i < 14; i++) {
        if (gLocalFlagBanks[i] == offset) {
            return i - 1; /* index 2 -> bank 1 */
        }
    }
    return 0; /* global / unknown -> no remap */
}

u32 Port_RemapBaselineLocalFlag(u32 offset, u32 ord) {
    int bank;
    const unsigned char(*remap)[FLAG_REMAP_TABLE_WIDTH];
    const unsigned char(*valid)[FLAG_REMAP_TABLE_WIDTH];

    if (gActiveRegion == TMC_REGION_USA) {
        return ord;
    }
    if (ord >= FLAG_REMAP_TABLE_WIDTH) {
        return PORT_FLAG_REMAP_INVALID;
    }
    bank = LocalBankNumberForOffset(offset);
    if (bank < 1 || bank > FLAG_REMAP_BANK_COUNT) {
        return ord;
    }
    if (gActiveRegion == TMC_REGION_EU) {
        remap = gFlagRemapEU;
        valid = gFlagRemapEUValid;
    } else {
        remap = gFlagRemapJP;
        valid = gFlagRemapJPValid;
    }
    if (!valid[bank - 1][ord]) {
        return PORT_FLAG_REMAP_INVALID;
    }
    return remap[bank - 1][ord];
}

u32 CheckLocalFlagB(u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord);
    return remapped == PORT_FLAG_REMAP_INVALID ? FALSE : CheckLocalFlagByBank(gArea.localFlagOffset, remapped);
}

void SetLocalFlagB(u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord);
    if (remapped != PORT_FLAG_REMAP_INVALID) {
        SetLocalFlagByBank(gArea.localFlagOffset, remapped);
    }
}

void ClearLocalFlagB(u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord);
    if (remapped != PORT_FLAG_REMAP_INVALID) {
        ClearLocalFlagByBank(gArea.localFlagOffset, remapped);
    }
}

/*
 * Explicit-bank variants for call sites whose bank does not come from
 * gArea.localFlagOffset (compiled tables carrying a bank + baseline ordinal
 * pair, e.g. WorldEvent rewards).
 */
bool32 CheckLocalFlagByBankB(u32 offset, u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(offset, ord);
    return remapped == PORT_FLAG_REMAP_INVALID ? FALSE : CheckLocalFlagByBank(offset, remapped);
}

void SetLocalFlagByBankB(u32 offset, u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(offset, ord);
    if (remapped != PORT_FLAG_REMAP_INVALID) {
        SetLocalFlagByBank(offset, remapped);
    }
}

void ClearLocalFlagByBankB(u32 offset, u32 ord) {
    u32 remapped = Port_RemapBaselineLocalFlag(offset, ord);
    if (remapped != PORT_FLAG_REMAP_INVALID) {
        ClearLocalFlagByBank(offset, remapped);
    }
}

/*
 * Multi-bit local check. Remap every semantic bit independently: insertions in
 * another region can make a USA-consecutive range non-contiguous. Preserve
 * CheckBits semantics: true only when every bit in the range is set.
 */
u32 CheckLocalFlagsB(u32 ord, u32 count) {
    u32 i;
    for (i = 0; i < count; ++i) {
        u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord + i);
        if (remapped == PORT_FLAG_REMAP_INVALID) {
            return FALSE;
        }
        if (!CheckLocalFlagByBank(gArea.localFlagOffset, remapped)) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Packed (type/length-encoded) variants for compiled-USA data. Global and room
 * flags are region-stable. Local ranges use the per-bit path above; writes
 * still target the single index used by SetFlag/ClearFlag.
 */
static u32 RemapPackedBaselineIndex(u32 flag) {
    u32 type = (flag & 0xc000) >> 0xe;
    if (type == 0) {
        u32 index = flag & 0x3ff;
        u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, index);
        if (remapped == PORT_FLAG_REMAP_INVALID) {
            return PORT_FLAG_REMAP_INVALID;
        }
        flag = (flag & ~0x3ffu) | (remapped & 0x3ff);
    }
    return flag;
}

u32 CheckFlagsB(u32 flag) {
    u32 type = (flag & 0xc000) >> 0xe;
    if (type == 0) {
        u32 index = flag & 0x3ff;
        u32 length = (((flag & ((0xf0) << 0x6)) >> 0xa) + 1);
        return CheckLocalFlagsB(index, length);
    }
    return CheckFlags(flag);
}

void SetFlagB(u32 flag) {
    flag = RemapPackedBaselineIndex(flag);
    if (flag != PORT_FLAG_REMAP_INVALID) {
        SetFlag(flag);
    }
}

void ClearFlagB(u32 flag) {
    flag = RemapPackedBaselineIndex(flag);
    if (flag != PORT_FLAG_REMAP_INVALID) {
        ClearFlag(flag);
    }
}
#endif /* PC_PORT && MULTI_REGION */
