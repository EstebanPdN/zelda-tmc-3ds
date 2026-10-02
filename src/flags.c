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
 * already region-correct and untouched; only C *baseline* references (literals,
 * named flag enums, compiled C const-table fields) are remapped via the *B
 * helpers below. The mapping is identity on USA (default config unaffected) and
 * identity outside the one diverging bank (LocalFlags1).
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
    if (gActiveRegion == TMC_REGION_USA) {
        return ord;
    }
    if (ord >= FLAG_REMAP_TABLE_WIDTH) {
        return ord;
    }
    bank = LocalBankNumberForOffset(offset);
    if (bank < 1 || bank > FLAG_REMAP_BANK_COUNT) {
        return ord;
    }
    if (gActiveRegion == TMC_REGION_EU) {
        return gFlagRemapEU[bank - 1][ord];
    }
    return gFlagRemapJP[bank - 1][ord];
}

u32 CheckLocalFlagB(u32 ord) {
    return CheckLocalFlagByBank(gArea.localFlagOffset, Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord));
}

void SetLocalFlagB(u32 ord) {
    SetLocalFlagByBank(gArea.localFlagOffset, Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord));
}

void ClearLocalFlagB(u32 ord) {
    ClearLocalFlagByBank(gArea.localFlagOffset, Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord));
}

/*
 * Explicit-bank variants for call sites whose bank does not come from
 * gArea.localFlagOffset (compiled tables carrying a bank + baseline ordinal
 * pair, e.g. WorldEvent rewards). Identity on USA and outside bank 1.
 */
bool32 CheckLocalFlagByBankB(u32 offset, u32 ord) {
    return CheckLocalFlagByBank(offset, Port_RemapBaselineLocalFlag(offset, ord));
}

void SetLocalFlagByBankB(u32 offset, u32 ord) {
    SetLocalFlagByBank(offset, Port_RemapBaselineLocalFlag(offset, ord));
}

void ClearLocalFlagByBankB(u32 offset, u32 ord) {
    ClearLocalFlagByBank(offset, Port_RemapBaselineLocalFlag(offset, ord));
}

/*
 * Multi-bit local check. Remaps the start ordinal; the `count` consecutive
 * baseline ordinals are assumed to remap to `count` consecutive region ordinals
 * (true for the only diverging bank, LocalFlags1, where adjacent flags shift by
 * the same delta — verified by tools/flag_remap_test.py). Identity on USA.
 */
u32 CheckLocalFlagsB(u32 ord, u32 count) {
    return CheckLocalFlagsByBank(gArea.localFlagOffset, Port_RemapBaselineLocalFlag(gArea.localFlagOffset, ord), count);
}

/*
 * Packed (type/length-encoded) variants for script/data-style call sites. Only
 * single-bit LOCAL flags are remapped: multi-bit groups (length>1) are not
 * guaranteed contiguous after remap, and global(type 1)/room(type 2) flags do
 * not diverge.
 */
static u32 RemapPackedBaseline(u32 flag) {
    u32 type = (flag & 0xc000) >> 0xe;
    u32 length = (((flag & ((0xf0) << 0x6)) >> 0xa) + 1);
    if (type == 0 && length == 1) {
        u32 index = flag & 0x3ff;
        u32 remapped = Port_RemapBaselineLocalFlag(gArea.localFlagOffset, index);
        flag = (flag & ~0x3ffu) | (remapped & 0x3ff);
    }
    return flag;
}

u32 CheckFlagsB(u32 flag) {
    return CheckFlags(RemapPackedBaseline(flag));
}

void SetFlagB(u32 flag) {
    SetFlag(RemapPackedBaseline(flag));
}

void ClearFlagB(u32 flag) {
    ClearFlag(RemapPackedBaseline(flag));
}
#endif /* PC_PORT && MULTI_REGION */
