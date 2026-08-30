/**
 * @file fileselect.c
 * @ingroup Tasks
 *
 * @brief File select task
 */
#include "fileselect.h"

#include "common.h"
#include "figurineMenu.h"
#include "game.h"
#include "item.h"
#include "kinstone.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "flags.h"
#include "room.h"
#include "player.h"
#include "screen.h"
#include "subtask.h"
#include "ui.h"
#include "affine.h"
#include "gfx.h"
#include "fade.h"
#ifdef PC_PORT
#include "port_bottle_compat.h"
#include "port_cloud_tops_fight.h"
#include "port_vaati_progress.h"
#include "port_ppu.h"
#include "port_runtime_config.h"
#include "port_save.h"
#include "port_tts.h"
#endif
#include <stdio.h>

// copy, erase, start
#define NUM_FILE_OPERATIONS 3

typedef enum {
    STATE_NONE,
    STATE_NEW,
    STATE_CHOOSE_LANG,
    STATE_OPTIONS,
    STATE_VIEW,
    STATE_COPY,
    STATE_ERASE,
    STATE_START,
#ifdef PC_PORT
    STATE_RANDOMIZER_CONFIG,
#endif
} FileSelectState;

// todo: does this belong with gFileSelectState?
typedef enum {
    SAVE_EMPTY = 0,
    SAVE_VALID = 1,
    SAVE_DELETED = 0xFFFFFFFF,
} SaveStatus;

typedef enum {
    RESULT_INVALID = 0,
    RESULT_ASCII = 1,
} CharResult;

const struct_080FC3E4 gUnk_080FC3E4[] = {
    { 0, 0, 128, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
#if defined(EU) && !defined(MULTI_REGION)
    { 0, 0, 0, 1 },
#else
    { 0, 0, 2, 1 },
#endif
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, KINSTONE_2C, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 7, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
#if defined(EU) && !defined(MULTI_REGION)
    { 0, 0, 2, 1 },
#else
    { 0, 0, 3, 1 },
#endif
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
#if (defined(EU) || defined(JP)) && !defined(MULTI_REGION)
    { 0, 0x12, UNK_6_40, 1 },
#else
    { 0, 5, UNK_6_20, 1 },
#endif
    { 0, 0, 0, 1 },
    { FLAG_BANK_4, YAMADOUKUTU_0E_SENNIN, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 1, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, UNK_6_20, 1 },
    { 0, KINSTONE_30, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 1 },
    { FLAG_BANK_4, GORON_DOUKUTU_APPEAR, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 7, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 2, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 6, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 7, 0 },
#if defined(EU) && !defined(MULTI_REGION)
    { 0, 0, 0, 0 },
#else
    { 0, 0, 2, 0 },
#endif
    { 0, KINSTONE_33, UNK_6_10, 1 },
    { 0, 1, UNK_6_20, 0 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 5, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
#if defined(EU) && !defined(MULTI_REGION)
    { 0, 0, 3, 2 },
    { 0, 0, 6, 2 },
#else
    { 0, 0, 6, 2 },
    { 0, 0, 3, 2 },
#endif
    { 0, 0, 5, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 0, 2 },
    { 0, 2, UNK_6_20, 3 },
    { 0, 3, UNK_6_20, 3 },
    { 0, 4, UNK_6_20, 3 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 2 },
    { 0, 0, 4, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 6, 1 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 3 },
    { 0, 0, 3, 3 },
    { FLAG_BANK_8, LV4_10_BOSSDIE, UNK_6_8, 3 },
    { 0, 0, 9, 0 },
    { 0, 0, 9, 1 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
};

/* Runtime region twins of gUnk_080FC3E4. Baseline above is USA (the fat
 * binary's static data). EU diverges at five table positions; JP diverges
 * only at the (EU||JP)-guarded entry. Readers default to the baseline table
 * and override per region under REGION_IS_*. Positions kept aligned with the
 * baseline so any index reads the region-correct row. */
const struct_080FC3E4 gUnk_080FC3E4_eu[] = {
    { 0, 0, 128, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 }, /* EU: base { 0, 0, 2, 1 } */
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, KINSTONE_2C, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 7, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 2, 1 }, /* EU: base { 0, 0, 3, 1 } */
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0x12, UNK_6_40, 1 }, /* EU||JP: base { 0, 5, UNK_6_20, 1 } */
    { 0, 0, 0, 1 },
    { FLAG_BANK_4, YAMADOUKUTU_0E_SENNIN, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 1, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, UNK_6_20, 1 },
    { 0, KINSTONE_30, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 1 },
    { FLAG_BANK_4, GORON_DOUKUTU_APPEAR, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 7, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 2, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 6, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 7, 0 },
    { 0, 0, 0, 0 }, /* EU: base { 0, 0, 2, 0 } */
    { 0, KINSTONE_33, UNK_6_10, 1 },
    { 0, 1, UNK_6_20, 0 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 5, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 }, /* EU: base { 0, 0, 6, 2 } (swapped pair) */
    { 0, 0, 6, 2 }, /* EU: base { 0, 0, 3, 2 } (swapped pair) */
    { 0, 0, 5, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 0, 2 },
    { 0, 2, UNK_6_20, 3 },
    { 0, 3, UNK_6_20, 3 },
    { 0, 4, UNK_6_20, 3 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 2 },
    { 0, 0, 4, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 6, 1 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 3 },
    { 0, 0, 3, 3 },
    { FLAG_BANK_8, LV4_10_BOSSDIE, UNK_6_8, 3 },
    { 0, 0, 9, 0 },
    { 0, 0, 9, 1 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
};

/* JP equals the USA baseline everywhere except the single (EU||JP)-guarded
 * entry, where JP takes the EU/JP value. */
const struct_080FC3E4 gUnk_080FC3E4_jp[] = {
    { 0, 0, 128, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, KINSTONE_2C, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 7, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0x12, UNK_6_40, 1 }, /* EU||JP: base { 0, 5, UNK_6_20, 1 } */
    { 0, 0, 0, 1 },
    { FLAG_BANK_4, YAMADOUKUTU_0E_SENNIN, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 1, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 5, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, 2, 1 },
    { 0, 0, UNK_6_20, 1 },
    { 0, KINSTONE_30, UNK_6_10, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 4, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 1 },
    { FLAG_BANK_4, GORON_DOUKUTU_APPEAR, UNK_6_40, 1 },
    { 0, 0, 3, 1 },
    { 0, 0, 3, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 7, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 2, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 4, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 3, 0 },
    { 0, 0, 6, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 7, 0 },
    { 0, 0, 2, 0 },
    { 0, KINSTONE_33, UNK_6_10, 1 },
    { 0, 1, UNK_6_20, 0 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 5, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 5, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 7, 3 },
    { 0, 0, 0, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 4, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 7, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 0, 2 },
    { 0, 2, UNK_6_20, 3 },
    { 0, 3, UNK_6_20, 3 },
    { 0, 4, UNK_6_20, 3 },
    { 0, 0, 2, 2 },
    { 0, 0, 6, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 0, 2 },
    { 0, 0, 3, 2 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 2 },
    { 0, 0, 4, 3 },
    { 0, 0, 7, 3 },
    { 0, 0, 6, 1 },
    { 0, 0, 0, 3 },
    { 0, 0, 1, 3 },
    { 0, 0, 3, 3 },
    { FLAG_BANK_8, LV4_10_BOSSDIE, UNK_6_8, 3 },
    { 0, 0, 9, 0 },
    { 0, 0, 9, 1 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0, 9, 3 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
    { 0, 0xffff, 0, 1 },
};

const Font gUnk_080FC844 = {
    &gBG0Buffer[0xcf], BG_TILE_ADDR(0x198), gTextGfxBuffer, 0, 96, 224, 1, 1, 0, 1, 3, 0, 0, 1, 0
};

const u16 gUnk_080FC85C[][3] = {
    { 0, 3, SFX_MENU_ERROR },
    { 0, 4, SFX_MENU_ERROR },
    { 1, 5, 0 },
    { 0, 6, SFX_MENU_ERROR },
    { 1, 7, 0 },
    { 1, 8, 0 },
    { 0, 9, SFX_MENU_ERROR },
    { 1, 10, 0 },
    { 1, 11, 0 },
    { 0, 12, SFX_MENU_ERROR },
    { 1, 13, 0 },
    { 0, 14, 0 },
};

static void HandleFileScreenEnter(void);
static void HandleFileScreenActive(void);
static void HandleFileScreenExit(void);
static void (*const sTaskHandlers[])(void) = {
    HandleFileScreenEnter,
    HandleFileScreenActive,
    HandleFileScreenExit,
};

static void HandleFileSelect(void);
extern void HandleFileNew(void);
static void HandleFileLanguageSelect(void);
extern void HandleFileOptions(void);
static void HandleFileView(void);
extern void HandleFileCopy(void);
extern void HandleFileDelete(void);
extern void HandleFileStart(void);
#ifdef PC_PORT
static void HandleFileRandoConfig(void);
#endif
static void (*const sFileScreenSubHandlers[])(void) = {
    HandleFileSelect,      HandleFileNew,  HandleFileLanguageSelect, HandleFileOptions,
    HandleFileView,        HandleFileCopy, HandleFileDelete,         HandleFileStart,
#ifdef PC_PORT
    HandleFileRandoConfig,
#endif
};

#ifdef PC_PORT
/* PC_PORT: GCC drops zero-length flexible array initializers.
 * Raw byte array keeps settingDict data contiguous. */
static const u8 gUnk_080FC8D0_raw[] = {
    0xFF, 0xD8, 0x00, 0xFF, 0xD8, 0x00, 0xE0, 0x10, 0x0F, /* struct fields */
    0x05, 0x00, 0x02, 0x00, 0xFF,                         /* settingDict */
};
#define gUnk_080FC8D0 (*(const KeyButtonLayout*)gUnk_080FC8D0_raw)
#else
static const KeyButtonLayout gUnk_080FC8D0 = {
    .aButtonX = 0xFF,
    .aButtonY = 0xD8,
    .aButtonText = 0x0,
    .bButtonX = 0xFF,
    .bButtonY = 0xD8,
    .bButtonText = 0x0,
    .rButtonX = 0xE0,
    .rButtonY = 0x10,
    .rButtonText = 0xF,
    .settingDict = { 0x5, 0x0, 0x2, 0x0, 0xFF },
};
#endif

static const u16 gUnk_080FC8DE[] = {
    0x01, 0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0, 0xc0, 0xa0, 0x80, 0x60, 0x40, 0x20,
};

#ifdef PC_PORT
#define PORT_SETTINGS_OPEN gChooseFileState.unk_0x0
#define PORT_SETTINGS_DIRTY gChooseFileState.unk_0x10
#define PORT_SETTINGS_ROW gChooseFileState.unk_0x20
#endif

#ifdef PC_PORT
/* Save-profile cycling. Step through `tmc[_*].sav` files discovered in cwd;
 * activating a profile re-reads EEPROM and refreshes the three save-slot
 * tiles below so the user can immediately see / pick from that profile's
 * data without leaving the title screen. */
#define PORT_SETTINGS_ROW_COUNT 5
#define PORT_PROFILE_ROW 4
extern const char* Port_Save_GetActivePath(void);
extern int Port_Save_SetActivePath(const char* path);
extern int Port_Save_ListProfiles(char (*out)[64], int max);
extern void Port_Config_SetActiveSaveProfile(const char* path);

/* Forward decl — defined later in this file. We need it for the
 * profile cycler so the slot tiles refresh on switch. */
static void ResetEmptyOrDeletedSaveFile(u32 index);

static void Port_FileSelect_CycleProfile(int direction) {
    char profiles[32][64];
    const int n = Port_Save_ListProfiles(profiles, 32);
    if (n == 0)
        return;

    const char* active = Port_Save_GetActivePath();
    int cur = 0;
    for (int i = 0; i < n; ++i) {
        if (strcmp(profiles[i], active) == 0) {
            cur = i;
            break;
        }
    }
    cur = (cur + direction + n) % n;

    if (!Port_Save_SetActivePath(profiles[cur]))
        return;
    Port_Config_SetActiveSaveProfile(profiles[cur]);

    /* Re-read the three engine save slots so the on-screen file tiles
     * reflect the freshly-switched profile's data immediately. */
    ResetEmptyOrDeletedSaveFile(0);
    ResetEmptyOrDeletedSaveFile(1);
    ResetEmptyOrDeletedSaveFile(2);
}

static const char* Port_FileSelect_ActiveProfileName(void) {
    return Port_Save_GetActivePath();
}
#else
#define PORT_SETTINGS_ROW_COUNT 4
#endif

static void sub_08050848(void);
static void sub_0805086C(void);
static void sub_08050940(void);
#ifdef PC_PORT
extern bool Port_RandoSave_LoadSlot(int slot);
extern void Rando_Reset(void);
extern bool Rando_IsActive(void);
extern void Rando_Runtime_OnNewFile(void);               /* port/rando/rando_runtime.c */
extern void Rando_Runtime_Refresh(void);                 /* port/rando/rando_runtime.c */
extern u32 WriteSaveFile(u32 index, SaveFile* saveFile); /* src/save.c */
#endif

#ifdef PC_PORT
static void DrawFileSelectSettingsHint(void);
static void HandlePortSettingsMenu(void);
static void DrawPortSettingsMenu(void);
#endif
#ifdef PC_PORT
extern bool Port_RandoFileMenu_ShouldOpenForNewFile(void);
extern void Port_RandoFileMenu_Open(int save_slot);
extern bool Port_RandoFileMenu_IsOpen(void);
#endif

void sub_08051358(void);
void sub_08051574(u32);
void sub_08051480(u32);
static void (*const sFileSelectDefaultHandlers[])(void) = {
    sub_08050848,
    sub_0805086C,
    sub_08050940,
};

extern void (*const gUnk_080FC944[])(void);
extern void (*const gUnk_080FC950[])(void);
extern void (*const gUnk_080FC960[])(void);
extern void (*const gUnk_080FC9B0[])(void);
extern void (*const gUnk_080FC9BC[])(void);
extern void (*const gUnk_080FC9C8[])(void);
extern const u8 gUnk_080FC970[];
extern const u8 gUnk_080FC980[];
extern const u8 gUnk_080FC9A0[];

static void ResetEmptyOrDeletedSaveFile(u32);
static void sub_0805066C(void);
static void HideButtonR(void);
static void ShowButtonR(void);
void sub_08051458(void);

void sub_08050888(void);
void sub_080508E4(void);
void sub_08050910(void);

void CreateDialogBox(u32 arg0, u32 arg1) {
    u32 sfx;
    Font var0;

    sub_08050384();
    MemCopy(&gUnk_080FC844, &var0, sizeof(gUnk_080FC844));
    sub_08056FEC(arg1, (u8*)GetTextVariableSlot(0));
    var0.gfx_src |= gUnk_080FC85C[arg0][0] << 0xC;
    ShowTextBox(gUnk_080FC85C[arg0][1], &var0);
    sfx = gUnk_080FC85C[arg0][2];
    if (sfx) {
        SoundReq(sfx);
    }
    gScreen.bg0.updated = 1;
}

void sub_08050384(void) {
    RecoverUI(0, 0);
    MemClear(&gBG0Buffer, sizeof(gBG0Buffer));
    gScreen.bg0.updated = 1;
}

void sub_080503A8(u32 gfxGroup) {
    LoadGfxGroup(gfxGroup);
    gScreen.bg1.updated = 1;
    gScreen.bg2.updated = 1;
}

void SetFileSelectState(FileSelectState mode) {
    gUI.state = mode;
    MemClear(&gBG0Buffer, sizeof(gBG0Buffer));
    MemClear(&gBG1Buffer, sizeof(gBG1Buffer));
#ifdef PC_PORT
    /* Announce the file-select sub-screen as the user transitions into
     * it. Each state lands on a distinct UI; this fires once per real
     * transition (the gUI.state assignment above is the gate). */
    {
        const char* name = NULL;
        switch (mode) {
            case STATE_NONE:
                name = "File select. Choose a save file.";
                break;
            case STATE_NEW:
                name = "New file. Enter a name.";
                break;
            case STATE_CHOOSE_LANG:
                name = "Language select.";
                break;
            case STATE_OPTIONS:
                name = "Options.";
                break;
            case STATE_VIEW:
                name = "File details. Press A to start, B to go back.";
                break;
            case STATE_COPY:
                name = "Copy file.";
                break;
            case STATE_ERASE:
                name = "Erase file.";
                break;
            case STATE_START:
                name = "Starting game.";
                break;
#ifdef PC_PORT
            case STATE_RANDOMIZER_CONFIG:
                name = "Randomizer settings.";
                break;
#endif
        }
        if (name) {
            PortTtsOptions opts = { 0 };
            opts.priority = PORT_TTS_PRIO_URGENT;
            opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
            opts.dedupe = true;
            Port_TTS_Speak(name, &opts);
        }
    }
#endif
}

void LoadOptionsFromSave(u32 idx) {
    u8 msg_speed;
    u8 brightness;

    if (idx >= NUM_SAVE_SLOTS) {
        // these apply to the language selection screen
        msg_speed = 1;
        brightness = 1;
    } else {
        SaveFile* saveFile = &gFileSelectState.saves[idx];
        msg_speed = saveFile->msg_speed;
        brightness = saveFile->brightness;
    }

    gSaveHeader->msg_speed = msg_speed;
    gSaveHeader->brightness = brightness;
    gUsedPalettes = 0xffffffff;
}

void SetActiveSave(u32 idx) {
    if (idx < NUM_SAVE_SLOTS) {
        gSaveHeader->saveFileId = idx;
        MemCopy(&gFileSelectState.saves[idx], &gSave, sizeof(gFileSelectState.saves[idx]));
    }
    LoadOptionsFromSave(idx);
#ifdef PC_PORT
    if (idx < NUM_SAVE_SLOTS) {
        if (!Port_RandoSave_LoadSlot((int)idx)) {
            Rando_Reset();
        } else if (Rando_IsActive() && !CheckGlobalFlag(EZERO_1ST)) {
            /* Crash-window heal: the sidecar was written before the first
             * EEPROM write of the new-file flow (Port_FileSelectRando_
             * StartSlot), and a crash in between leaves an ACTIVE seed on a
             * save that never received its one-shot grants (start inventory,
             * story-skip globals — a documented softlock). Every granted
             * rando save has EZERO_1ST set from birth (ApplyStorySkip), so
             * its absence identifies the torn state; grants are idempotent
             * flag/inventory writes, safe to re-apply. */
            fprintf(stderr, "[RANDO] slot %u: active seed without new-file grants (torn commit) — re-applying\n", idx);
            Rando_Runtime_OnNewFile();
            MemCopy(&gSave, &gFileSelectState.saves[idx], sizeof(gSave));
            WriteSaveFile(idx, &gSave);
        }

        if (!Rando_IsActive()) {
            SaveFile vaatiRepairBackup;
            bool32 legacyVaatiRepair =
                Port_Save_ReadVaatiProgressBackupSlot(idx, &vaatiRepairBackup, sizeof(vaatiRepairBackup)) &&
                Port_VaatiProgressBackupProvesLegacyRepair(&gSave, &vaatiRepairBackup);

            if (Port_VaatiProgressNeedsRepair(&gSave, legacyVaatiRepair)) {
                if (!Port_Save_PreserveBeforeVaatiProgressRepair()) {
                    fprintf(stderr,
                            "[SAVE] Refused Vaati progression repair for slot %u because the permanent backup "
                            "failed.\n",
                            idx);
                } else if (Port_RepairVaatiProgress(&gSave, legacyVaatiRepair)) {
                    MemCopy(&gSave, &gFileSelectState.saves[idx], sizeof(gSave));
                    if (WriteSaveFile(idx, &gSave) != 0) {
                        fprintf(stderr, "[SAVE] Reset a corrupted Vaati finale checkpoint in slot %u.\n", idx);
                    } else {
                        fprintf(stderr,
                                "[SAVE] Reset a corrupted Vaati finale checkpoint in slot %u in memory; durable "
                                "persistence will retry on the next save.\n",
                                idx);
                    }
                }
            }
        }

        if (Port_SmithBottleFlagsNeedRepair(&gSave, Rando_IsActive())) {
            if (!Port_Save_PreserveBeforeSmithBottleFlagRepair()) {
                fprintf(stderr,
                        "[SAVE] Refused Smith bottle flag repair for slot %u because the permanent backup failed.\n",
                        idx);
            } else if (Port_RepairSmithBottleFlags(&gSave, Rando_IsActive())) {
                MemCopy(&gSave, &gFileSelectState.saves[idx], sizeof(gSave));
                if (WriteSaveFile(idx, &gSave) != 0) {
                    fprintf(stderr, "[SAVE] Migrated the legacy EU Smith bottle flag in slot %u.\n", idx);
                } else {
                    fprintf(stderr,
                            "[SAVE] Migrated the legacy EU Smith bottle flag in slot %u in memory; durable "
                            "persistence will retry on the next save.\n",
                            idx);
                }
            }
        }

        if (!Rando_IsActive()) {
            SaveFile cloudTopsRepairBackup;
            bool32 legacyInventoryRepair = FALSE;
            bool32 needsFightReplay;
            u32 backupIdx;

            if (Port_CloudTopsMayNeedLegacyInventoryRepair(&gSave)) {
                /* A user may have copied the affected slot after E9 inserted
                 * the item.  Search every slot in the preserved pre-E9
                 * profile, but let the semantic matcher authorize only the
                 * matching predecessor. */
                for (backupIdx = 0; backupIdx < NUM_SAVE_SLOTS && !legacyInventoryRepair; ++backupIdx) {
                    legacyInventoryRepair =
                        Port_Save_ReadCloudTopsRepairBackupSlot(backupIdx, &cloudTopsRepairBackup,
                                                                sizeof(cloudTopsRepairBackup)) &&
                        Port_CloudTopsBackupProvesLegacyInventoryRepair(&gSave, &cloudTopsRepairBackup);
                }
            }
            needsFightReplay = Port_CloudTopsNeedsFightReplay(&gSave, legacyInventoryRepair);

            if (needsFightReplay && !Port_Save_PreserveBeforeCloudTopsRepair()) {
                fprintf(stderr,
                        "[SAVE] Refused Cloud Tops fight repair for slot %u because the permanent backup "
                        "failed.\n",
                        idx);
            } else if (needsFightReplay && Port_RepairCloudTopsFight(&gSave, legacyInventoryRepair)) {
                MemCopy(&gSave, &gFileSelectState.saves[idx], sizeof(gSave));
                if (WriteSaveFile(idx, &gSave) != 0) {
                    fprintf(stderr,
                            "[SAVE] Reopened the corrupted Cloud Tops piranha fight and reward in slot %u.\n",
                            idx);
                } else {
                    fprintf(stderr,
                            "[SAVE] Reopened the corrupted Cloud Tops piranha fight and reward in slot %u in "
                            "memory; durable persistence will retry on the next save.\n",
                            idx);
                }
            }
        }
    } else {
        Rando_Reset();
    }
    /* Recompute eventdefine-driven runtime state (damage multiplier,
     * beep/music mutes) for whichever seed is now active (or none). */
    Rando_Runtime_Refresh();
#endif
}

#ifdef PC_PORT
void Port_FileSelectRando_StartSlot(int slot) {
    if ((u32)slot < NUM_SAVE_SLOTS) {
        gFileSelectState.saveStatus[slot] = SAVE_VALID;
        SetActiveSave((u32)slot);
        if (Rando_IsActive()) {
            /* New-rando-file commit point: the slot was initialized and
             * written by the vanilla new-file flow (sub_080513C0) before
             * the rando config opened, and SetActiveSave() just loaded it
             * into gSave + activated the sidecar seed. Apply the seed's
             * one-shot grants (start inventory, crests, portals, instant
             * text) and persist them so they survive a quit without an
             * in-game save. Sidecar reloads of existing saves never pass
             * through here, so grants apply exactly once per new file. */
            Rando_Runtime_OnNewFile();
            MemCopy(&gSave, &gFileSelectState.saves[slot], sizeof(gSave));
            WriteSaveFile((u32)slot, &gSave);
        }
    }
    SoundReq(SONG_VOL_FADE_OUT);
    SetFileSelectState(STATE_START);
}

void Port_FileSelectRando_CancelSlot(int slot) {
    (void)slot;
    SoundReq(SFX_MENU_CANCEL);
    SetFileSelectState(STATE_NONE);
}
#endif

void FileSelectTask(void) {
    FlushSprites();
    sTaskHandlers[gMain.state]();
    if (gUI.lastState != gUI.state) {
        gUI.lastState = gUI.state;
        gScreen.bg0.xOffset = 0;
        gScreen.bg0.yOffset = 0;
        gScreen.bg1.xOffset = 0;
        gScreen.bg1.yOffset = 0;
        gScreen.bg2.xOffset = 0;
        gScreen.bg2.yOffset = 0;
        MemClear(&gChooseFileState, sizeof(gChooseFileState));
    }

    HideButtonR();
    sFileScreenSubHandlers[gUI.lastState]();

    gFileSelectState.isTransitioning = FALSE;
    UpdateEntities();
    sub_0805066C();
    UpdateUIElements();
    DrawUIElements();
    DrawEntities();
    CopyOAM();
    if (gFileSelectState.unk3 != gSaveHeader->language) {
        gFileSelectState.unk3 = gSaveHeader->language;
        sub_080503A8(0x6);
        sub_080503A8(0xF);
    }
}

static void HandleFileScreenEnter(void) {
    u32 i;

    DispReset(1);
    InitSoundPlayingInfo();
#ifdef PC_PORT
    gba_MemClear(VRAM, 0x80); // clear palettes
#else
    MemClear((void*)VRAM, 0x80);
#endif
    MessageInitialize();
    EraseAllEntities();
    ClearTileMaps();
    ResetPalettes();
    ResetPaletteTable(0);
    MemClear(&gHUD, sizeof(gHUD));
    MemClear(&gFileSelectState, sizeof(gFileSelectState));
    gFileSelectState.unk3 = 7;
    gFileSelectState.unk6 = gSaveHeader->language > LANGUAGE_EN ? 3 : 0;
    MemClear(&gUI, sizeof(gUI));
    gUI.lastState = 8;
    SetFileSelectState(STATE_NONE);
    InitDMA();
    ResetEmptyOrDeletedSaveFile(0);
    ResetEmptyOrDeletedSaveFile(1);
    ResetEmptyOrDeletedSaveFile(2);
    InitVBlankDMA();
    sub_080503A8(0x5);
    LoadPaletteGroup(0x9);
    for (i = 0; i < 26; i++) {
        CreateObject(FILE_SCREEN_OBJECTS, i, 0);
    }

    sub_080A70AC(&gUnk_080FC8D0);
    HideButtonR();
    gScreen.lcd.displayControl |= DISPCNT_BG_ALL_ON | DISPCNT_OBJ_ON;
    gScreen.bg3.control = BGCNT_PRIORITY(3) | BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(30);
    gScreen.controls.layerFXControl = BLDCNT_TGT1_BG2 | BLDCNT_TGT2_BG3 | BLDCNT_EFFECT_BLEND;
    gScreen.controls.alphaBlend = BLDALPHA_BLEND(15, 10);
    gGFXSlots.unk0 = 1;
    gMain.state = GAMETASK_INIT;
    SoundReq(BGM_FILE_SELECT);
    SetFade(FADE_INSTANT, 8);
}

static void HandleFileScreenActive(void) {
}

static void HandleFileScreenExit(void) {
    if (!gFadeControl.active) {
#ifdef PC_PORT
        /* Reborn parity: tell the parity layer a resume just happened
         * so the next queued Ezlo hint can be suppressed (if the
         * toggle is on). Harmless on a fresh new-game start because
         * no hint is queued — the flag just gets consumed silently. */
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
#endif
        SetTask(TASK_GAME);
    }
}

static void ResetEmptyOrDeletedSaveFile(u32 index) {
    SaveFile* saveFile = &gFileSelectState.saves[index];
    int status = ReadSaveFile(index, saveFile);
    switch (status) {
        case SAVE_DELETED:
            SetFileStatusDeleted(index);
            // fallthrough
        case SAVE_EMPTY:
            ResetSaveFile(index);
            break;
    }
    gFileSelectState.saveStatus[index] = status;
}

static void sub_0805066C(void) {
    u32 loadNewPalette;
    const u8* paletteOffset;

    loadNewPalette = FALSE;
    if (--gFileSelectState.unk1 == 0) {
        gFileSelectState.unk1 = 16;
        gFileSelectState.unk2 = (gFileSelectState.unk2 + 1) % 15;
        loadNewPalette = TRUE;
    }

    if (gFileSelectState.unk2 == 0) {
        gFileSelectState.unk2 = 1;
        gFileSelectState.unk1 = (Random() & 0x7) * 16 + 8;
        loadNewPalette = TRUE;
    }

    if (loadNewPalette) {
        paletteOffset = &gGlobalGfxAndPalettes[gUnk_080FC8DE[gFileSelectState.unk2]];
        if (REGION_IS_EU) {
            LoadPalettes(&paletteOffset[0x11A60], 11, 1);
            LoadPalettes(&paletteOffset[0x11B60], 12, 1);
            LoadPalettes(&paletteOffset[0x11C60], 13, 1);
            LoadPalettes(&paletteOffset[0x11D60], 14, 1);
        } else {
            LoadPalettes(&paletteOffset[0x11AA0], 11, 1);
            LoadPalettes(&paletteOffset[0x11BA0], 12, 1);
            LoadPalettes(&paletteOffset[0x11CA0], 13, 1);
            LoadPalettes(&paletteOffset[0x11DA0], 14, 1);
        }
    }
}

void sub_0805070C(void) {
    u32 i;
    int j;
    WStruct* var0;
    char* name;

    var0 = sub_0805F2C8();
    if (var0) {
        var0->charColor = 0;
        var0->bgColor = 5;
        var0->unk1 = 1;
        var0->unk4 = 0x30;
        var0->unk8 = gTextGfxBuffer;
        for (i = 0; i < NUM_SAVE_SLOTS; i++) {
            var0->unk6 = 0;
            MemClear(var0->unk8, 0x200);
            name = &gFileSelectState.saves[i].name[0];
            for (j = 0; j < FILENAME_LENGTH; j++) {
                sub_0805F7DC(name[j], var0);
            }
            // i is a struct of size 0x200
#ifdef PC_PORT
            // Use native-to-GBA copy since var0->unk8 is a native pointer
            port_MemCopyToGBA(var0->unk8, 0x06014000 + i * 0x200, 0x200);
#else
            MemCopy(var0->unk8, (void*)(OBJ_VRAM0 + 0x4000 + i * 0x200), 0x200);
#endif
        }
        sub_0805F300(var0);
    }
}

void sub_08050790(void) {
    u32 i, j;
    WStruct* var0;
    u32 var1;

    var0 = sub_0805F2C8();
    if (var0) {
        var0->charColor = 0;
        var0->bgColor = 5;
        var0->unk1 = 1;
        var0->unk4 = 0x80;
        var0->unk8 = gTextGfxBuffer;
        for (i = 0; i < 16; i++) {
            var0->unk6 = 0;
            var1 = i * 16;
            for (j = 0; j < 16; j++) {
                sub_0805F7DC(var1, var0);
                var1++;
            }
#ifdef PC_PORT
            port_MemCopyToGBA(gTextGfxBuffer, BG_VRAM + i * 0x400, 0x400);
#else
            MemCopy(gTextGfxBuffer, (void*)(BG_VRAM + i * 0x400), 0x400);
#endif
        }
        sub_0805F300(var0);
    }
}

static void HideButtonR(void) {
    gHUD.buttonX[2] = 0x140;
    gHUD.buttonY[2] = 0x24;
}

static void ShowButtonR(void) {
    gHUD.buttonX[2] = 0xD0;
    gHUD.buttonY[2] = 0x24;
}

static void HandleFileSelect(void) {

#ifdef PC_PORT
    if (Port_Config_PortSettingsMenuEnabled() && PORT_SETTINGS_OPEN) {
        HandlePortSettingsMenu();
        return;
    }
#endif

    sFileSelectDefaultHandlers[gChooseFileState.state]();
    sub_08050A64(gFileSelectState.unk6);
#ifdef PC_PORT
    if (Port_Config_PortSettingsMenuEnabled()) {
        DrawFileSelectSettingsHint();
    }
#endif
}

void sub_08050848(void) {
    sub_080503A8(0x7);
    sub_0805070C();
    gFileSelectState.unk7 = 0;
    sub_08050AFC(gFileSelectState.unk6);
    SetMenuType(1);
}

void (*const gUnk_080FC908[])(void) = {
    sub_08050888,
    sub_080508E4,
    sub_08050910,
};

void sub_0805086C(void) {
    gUnk_080FC908[gChooseFileState.subState]();
}

// transitioning away from submenu
void sub_08050888(void) {
    if (!gFadeControl.active) {
        switch (gFileSelectState.saveStatus[gFileSelectState.unk7]) {
            case SAVE_EMPTY:
                ResetSaveFile(gFileSelectState.unk7);
                gChooseFileState.subState = 2;
                break;
            case SAVE_VALID:
                gChooseFileState.subState = 2;
                break;
            default:
                ResetSaveFile(gFileSelectState.unk7);
                CreateDialogBox(0, gFileSelectState.unk7 + 1);
                gChooseFileState.timer = 30;
                gChooseFileState.subState = 1;
                break;
        }
    }
}

void sub_080508E4(void) {
    if (gChooseFileState.timer == 0) {
        if (gInput.newKeys & (A_BUTTON | START_BUTTON)) {
            gChooseFileState.subState = 2;
        }
    } else {
        gChooseFileState.timer--;
    }
}

void sub_08050910(void) {
    sub_08050384();
    if (++gFileSelectState.unk7 > 2) {
        SetMenuType(2);
    } else {
        gChooseFileState.subState = 0;
    }
}

void sub_08050940(void) {
    int row_idx;
    u32 keys;
    int num_rows;
    FileSelectState mode;

    if (gFileSelectState.isTransitioning) {
        return;
    }

    row_idx = gFileSelectState.unk6;
    keys = gInput.newKeys;
#ifdef PC_PORT
    /* Debug: TMC_AUTOLOAD=N env var auto-selects save slot N once at
     * file-select entry. Lets headless reproductions (issue repros) skip
     * the manual click-through. */
    {
        static int sAutoLoadFired = 0;
        if (!sAutoLoadFired) {
            const char* al = getenv("TMC_AUTOLOAD");
            if (al && *al >= '0' && *al <= '2') {
                sAutoLoadFired = 1;
                row_idx = *al - '0';
                gFileSelectState.unk6 = row_idx;
                keys = A_BUTTON;
                fprintf(stderr, "[autoload] selecting save slot %d\n", row_idx);
            }
        }
    }
#endif
    if ((gInput.heldKeys & L_BUTTON) && gFileSelectState.saveStatus[row_idx] == SAVE_VALID) {
        keys &= ~(DPAD_UP | DPAD_DOWN);
    }

    num_rows = gSaveHeader->language > 1 ? NUM_SAVE_SLOTS + 1 : NUM_SAVE_SLOTS;
    mode = gUI.lastState;
    switch (keys) {
        case DPAD_UP:
            if (row_idx > 0)
                row_idx--;
            break;
        case DPAD_DOWN:
            if (num_rows - 1 > row_idx)
                row_idx++;
            break;
        case R_BUTTON:
            if (row_idx < NUM_SAVE_SLOTS && gFileSelectState.saveStatus[row_idx] == SAVE_VALID)
                mode = STATE_OPTIONS;
            break;
#ifdef PC_PORT
        case L_BUTTON:
            if (Port_Config_PortSettingsMenuEnabled()) {
                extern void Port_RandoFileMenu_ToggleSidebar(void);
                Port_RandoFileMenu_ToggleSidebar();
                SoundReq(SFX_TEXTBOX_SELECT);
            }
            break;
#endif
        case A_BUTTON:
        case START_BUTTON:
            SetActiveSave(row_idx);
            if (row_idx == 3)
                mode = STATE_CHOOSE_LANG;
            else
                switch (gFileSelectState.saveStatus[row_idx]) {
                    case SAVE_EMPTY:
                        mode = STATE_NEW;
                        break;
                    case SAVE_VALID:
                        mode = STATE_VIEW;
                        break;
                }
            break;
    }

    if (gUI.lastState != mode) {
        SetFileSelectState(mode);
        SoundReq(SFX_TEXTBOX_SELECT);
    }

    row_idx = (row_idx + num_rows) % num_rows;
    if (gFileSelectState.unk6 != row_idx) {
        gFileSelectState.unk6 = row_idx;
        sub_08050AFC(row_idx);
        SoundReq(SFX_TEXTBOX_CHOICE);
    }

    if (gFileSelectState.saveStatus[gFileSelectState.unk6] == SAVE_VALID) {
        ShowButtonR();
    }
}

#ifdef PC_PORT
static void DrawFileSelectSettingsHint(void) {
    static const u8 sSettingsHintText[] = "L Settings";
    Font font;

    gChooseFileState.unk_0x12 = 1;
    MemClear(&gBG0Buffer[0x34], 0x40);
    gScreen.bg0.updated = 1;

    MemCopy(&gUnk_080FC844, &font, sizeof(font));
    font.dest = &gBG0Buffer[0x34];
    font.width = 0x60;
    font.right_align = 0;
    font.sm_border = 0;
    font.draw_border = 0;
    ShowTextBox((uintptr_t)sSettingsHintText, &font);
    gScreen.bg0.updated = 1;
}

static void HandlePortSettingsMenu(void) {
    s32 row;

    row = PORT_SETTINGS_ROW;
    switch (gInput.newKeys) {
        case DPAD_UP:
            row--;
            SoundReq(SFX_TEXTBOX_CHOICE);
            break;
        case DPAD_DOWN:
            row++;
            SoundReq(SFX_TEXTBOX_CHOICE);
            break;
        case DPAD_LEFT:
            if (row == 0) {
                Port_PPU_CycleWindowScale(-1);
            } else if (row == 1) {
                Port_PPU_CyclePresentationMode(-1);
            } else if (row == 2) {
                Port_Config_CycleTargetFps(-1);
            } else if (row == 3) {
                Port_PPU_ToggleFullscreen();
#ifdef PC_PORT
            } else if (row == PORT_PROFILE_ROW) {
                Port_FileSelect_CycleProfile(-1);
            }
#endif
            PORT_SETTINGS_DIRTY = TRUE;
            SoundReq(SFX_TEXTBOX_CHOICE);
            break;
        case DPAD_RIGHT:
        case A_BUTTON:
        case START_BUTTON:
            if (row == 0) {
                Port_PPU_CycleWindowScale(1);
            } else if (row == 1) {
                Port_PPU_CyclePresentationMode(1);
            } else if (row == 2) {
                Port_Config_CycleTargetFps(1);
            } else if (row == 3) {
                Port_PPU_ToggleFullscreen();
#ifdef PC_PORT
            } else if (row == PORT_PROFILE_ROW) {
                Port_FileSelect_CycleProfile(1);
            }
#endif
            PORT_SETTINGS_DIRTY = TRUE;
            SoundReq(SFX_TEXTBOX_CHOICE);
            break;
        case B_BUTTON:
        case L_BUTTON:
            PORT_SETTINGS_OPEN = FALSE;
            PORT_SETTINGS_DIRTY = FALSE;
            gChooseFileState.unk_0x12 = 0;
            MemClear(&gBG0Buffer, sizeof(gBG0Buffer));
            gScreen.bg0.updated = 1;
            SoundReq(SFX_MENU_CANCEL);
            return;
    }

    row = (row + PORT_SETTINGS_ROW_COUNT) % PORT_SETTINGS_ROW_COUNT;
    if (PORT_SETTINGS_ROW != row) {
        PORT_SETTINGS_ROW = row;
        PORT_SETTINGS_DIRTY = TRUE;
    }

    if (PORT_SETTINGS_DIRTY) {
        DrawPortSettingsMenu();
        PORT_SETTINGS_DIRTY = FALSE;
    }
}

static void DrawPortSettingsMenu(void) {
    static char sPortSettingsText[256];
    static char sFpsText[16];
    Font font;
    s32 row;
    u32 fps;

    row = PORT_SETTINGS_ROW;
    fps = Port_Config_TargetFps();
    if (fps == 0) {
        strcpy(sFpsText, "uncapped");
    } else {
        snprintf(sFpsText, sizeof(sFpsText), "%u", (unsigned)fps);
    }
#ifdef PC_PORT
    snprintf(sPortSettingsText, sizeof(sPortSettingsText),
             "PORT SETTINGS\n\n"
             "%c Scale %ux\n"
             "%c Filter %s\n"
             "%c FPS %s\n"
             "%c Fullscreen %s\n"
             "%c Profile %s\n\n"
             "A Change  B Back",
             row == 0 ? '>' : ' ', (unsigned)Port_PPU_WindowScale(), row == 1 ? '>' : ' ',
             Port_PPU_PresentationModeName(), row == 2 ? '>' : ' ', sFpsText, row == 3 ? '>' : ' ',
             Port_PPU_IsFullscreen() ? "on" : "off", row == PORT_PROFILE_ROW ? '>' : ' ',
             Port_FileSelect_ActiveProfileName());
#else
                snprintf(sPortSettingsText, sizeof(sPortSettingsText),
                         "PORT SETTINGS\n\n"
                         "%c Scale %ux\n"
                         "%c Filter %s\n"
                         "%c FPS %s\n"
                         "%c Fullscreen %s\n\n"
                         "A Change  B Back",
                         row == 0 ? '>' : ' ', (unsigned)Port_PPU_WindowScale(), row == 1 ? '>' : ' ',
                         Port_PPU_PresentationModeName(), row == 2 ? '>' : ' ', sFpsText, row == 3 ? '>' : ' ',
                         Port_PPU_IsFullscreen() ? "on" : "off");
#endif

    MemClear(&gBG0Buffer, sizeof(gBG0Buffer));
    MemCopy(&gUnk_080FC844, &font, sizeof(font));
    font.dest = &gBG0Buffer[0x85];
    font.width = 0xB0;
    font.right_align = 0;
    font.sm_border = 1;
    font.draw_border = 1;
    ShowTextBox((uintptr_t)sPortSettingsText, &font);
    gScreen.bg0.updated = 1;
}
#endif

void sub_08050A64(u32 idx) {
    if (idx >= NUM_SAVE_SLOTS || gFileSelectState.saveStatus[idx] != SAVE_VALID) {
        return;
    }

    gOamCmd._4 = 0;
    gOamCmd._6 = 0;
    gOamCmd._8 = 0x400;

    // Draw the save file's obtained elements.
    if (GetInventoryValue(ITEM_EARTH_ELEMENT)) {
        gOamCmd.x = 0xA2;
        gOamCmd.y = 0x36;
        if (REGION_IS_EU) {
            DrawDirect(0x144, 0x24);
        } else {
            DrawDirect(0x145, 0x24);
        }
    }
    if (GetInventoryValue(ITEM_FIRE_ELEMENT)) {
        gOamCmd.x = 0x96;
        gOamCmd.y = 0x3D;
        if (REGION_IS_EU) {
            DrawDirect(0x144, 0x22);
        } else {
            DrawDirect(0x145, 0x22);
        }
    }
    if (GetInventoryValue(ITEM_WATER_ELEMENT)) {
        gOamCmd.x = 0xAE;
        gOamCmd.y = 0x3D;
        if (REGION_IS_EU) {
            DrawDirect(0x144, 0x23);
        } else {
            DrawDirect(0x145, 0x23);
        }
    }
    if (GetInventoryValue(ITEM_WIND_ELEMENT)) {
        gOamCmd.x = 0xA2;
        gOamCmd.y = 0x44;
        if (REGION_IS_EU) {
            DrawDirect(0x144, 0x21);
        } else {
            DrawDirect(0x145, 0x21);
        }
    }
}

void sub_08050B3C(u16*);

void sub_08050AFC(u32 idx) {
    SetActiveSave(idx);
    MemClear(&gBG1Buffer, sizeof(gBG1Buffer));
    if (gFileSelectState.saveStatus[idx] == SAVE_VALID) {
        sub_08050B3C(&gBG1Buffer[0x14E]);
    }
    gScreen.bg1.updated = 1;
#ifdef PC_PORT
    /* Announce the currently-focused save slot row so a screen-reader
     * user knows which file they're on. Called once per cursor move
     * (caller already gates on row-idx change). Slots 0..2 are the
     * three save files; idx 3 is the language toggle row that only
     * shows in multi-language builds. */
    {
        char buf[96];
        if (idx < NUM_SAVE_SLOTS) {
            const char* status = "empty";
            switch ((SaveStatus)gFileSelectState.saveStatus[idx]) {
                case SAVE_VALID:
                    status = "in progress";
                    break;
                case SAVE_DELETED:
                    status = "deleted";
                    break;
                case SAVE_EMPTY:
                    status = "empty";
                    break;
            }
            snprintf(buf, sizeof(buf), "File %u, %s.", (unsigned)(idx + 1), status);
        } else {
            snprintf(buf, sizeof(buf), "Language select.");
        }
        PortTtsOptions opts = { 0 };
        opts.priority = PORT_TTS_PRIO_URGENT;
        opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
        opts.dedupe = true;
        Port_TTS_Speak(buf, &opts);
    }
#endif
}

typedef struct {
    u16* unk0;
    u32 unk4;
} unk_08050B3C;

extern const u16 gUnk_080FC914[];

const u16 gUnk_080FC914[] = { 0xf251, 0xf251, 0xf251, 0xf251, 0xf251, 0xf251, 0xf251, 0xf251, 0xf251, 0xf251,
                              0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d, 0xf24d };

// Handles drawing of hearts
void sub_08050B3C(u16* target) {
    s32 currentQuarterHearts;
    s32 maxQuarterHearts;
    s32 maxHeartsFirstRow;
    s32 currentFullHearts;
    s32 maxHeartsSecondRow;
    u32 currentFullHeartsFirstRow;
    s32 currentFullHeartsSecondRow;
    u16* temp_target;
    u32 currentPartialHeartValue;
    u32 maxHearts;

    currentQuarterHearts = gSave.stats.health / 2;
    if (currentQuarterHearts == 0) {
        currentQuarterHearts = 1;
    }

    maxQuarterHearts = gSave.stats.maxHealth / 2;
    if (maxQuarterHearts == 0) {
        return;
    }

    if (currentQuarterHearts > maxQuarterHearts) {
        currentQuarterHearts = maxQuarterHearts;
    }

    currentPartialHeartValue = currentQuarterHearts & 3;
    currentFullHearts = currentQuarterHearts / 4;
    currentFullHeartsFirstRow = currentFullHearts;
    if (currentFullHearts > 10) {
        currentFullHeartsFirstRow = 10;
    }

    currentFullHeartsSecondRow = currentFullHearts - 10;
    if (currentFullHeartsSecondRow < 0) {
        currentFullHeartsSecondRow = 0;
    }

    maxHearts = maxQuarterHearts / 4;
    maxHeartsFirstRow = maxHearts;
    if (maxHeartsFirstRow > 10) {
        maxHeartsFirstRow = 10;
    }
    maxHeartsSecondRow = maxHearts - 10;
    temp_target = target;
    temp_target[0] = 0xF24C;
    DmaCopy16(3, &gUnk_080FC914[10 - currentFullHeartsFirstRow], &temp_target[1], maxHeartsFirstRow * 2);
    if (maxHeartsSecondRow > 0) {
        temp_target += 0x20;
        temp_target[0] = 0xF24C;
        DmaCopy16(3, &gUnk_080FC914[10 - currentFullHeartsSecondRow], &temp_target[1], maxHeartsSecondRow * 2);
    }

    if (!currentPartialHeartValue) {
        return;
    }

    temp_target = target;
    if (currentFullHearts >= 10) {
        currentFullHearts = currentFullHearts - 10;
        temp_target += 0x20;
    }

    temp_target[currentFullHearts + 1] = currentPartialHeartValue - 0xDB3;
}

void sub_08050C54(void);
void sub_08050D68(void);

void (*const gUnk_080FC93C[])() = {
    sub_08050C54,
    sub_08050D68,
};

void HandleFileView(void) {
    gUnk_080FC93C[gMenu.menuType]();
    sub_08050A64(gFileSelectState.unk6);
}

void sub_08050C54(void) {
    s32 column_idx;
    u32 vkeys;

    if (gFileSelectState.isTransitioning)
        return;

    column_idx = gMenu.column_idx;
    vkeys = gInput.newKeys;
#ifdef PC_PORT
    {
        static int sAutoViewFired = 0;
        if (!sAutoViewFired && getenv("TMC_AUTOLOAD")) {
            sAutoViewFired = 1;
            vkeys = A_BUTTON;
            column_idx = 0; /* Start column */
            gMenu.column_idx = 0;
            fprintf(stderr, "[autoload] starting selected save\n");
        }
    }
#endif
    switch (vkeys) {
        case A_BUTTON:
        case START_BUTTON:
            if (column_idx == 0) {
                SoundReq(SONG_VOL_FADE_OUT);
            }
            gMenu.transitionTimer = 15;
            SetMenuType(1);
            SoundReq(SFX_TEXTBOX_SELECT);
            break;
        case B_BUTTON:
            SoundReq(SFX_MENU_CANCEL);
            SetFileSelectState(STATE_NONE);
            break;
        case DPAD_LEFT:
            column_idx--;
            break;
        case DPAD_RIGHT:
            column_idx++;
            break;
    }
    if (column_idx < 0) {
        column_idx = 0;
    }
    if (column_idx >= NUM_FILE_OPERATIONS) {
        column_idx = NUM_FILE_OPERATIONS - 1;
    }
    if (gMenu.column_idx != column_idx) {
        gMenu.column_idx = column_idx;
        SoundReq(SFX_TEXTBOX_CHOICE);
    }
}

void sub_08050D68(void) {
    u32 state;

    if (gMenu.transitionTimer == 0) {
        switch (gMenu.column_idx) {
            default:
            case 0:
                state = STATE_START;
                break;
            case 1:
                state = STATE_COPY;
                break;
            case 2:
                state = STATE_ERASE;
                break;
        }
        SetFileSelectState(state);
    } else {
        gMenu.transitionTimer--;
    }
}

void sub_08050DB8(void);
void sub_08050DE4(void);
void sub_08050E88(void);
void (*const gUnk_080FC944[])(void) = {
    sub_08050DB8,
    sub_08050DE4,
    sub_08050E88,
};

void HandleFileLanguageSelect(void) {
    gUnk_080FC944[gMenu.menuType]();
}

void sub_08050DB8(void) {
    MemClear(&gBG2Buffer, sizeof(gBG2Buffer));
    sub_080503A8(0xc);
    gMenu.field_0x4 = gSaveHeader->language;
    SetMenuType(1);
}

void sub_08050DE4(void) {
    s32 row_idx;

    if (gFileSelectState.isTransitioning)
        return;

    row_idx = gSaveHeader->language;
    switch (gInput.newKeys) {
        case DPAD_UP:
            row_idx--;
            break;
        case DPAD_DOWN:
            row_idx++;
            break;
        case A_BUTTON:
        case START_BUTTON:
            SoundReq(SFX_TEXTBOX_SELECT);
            if (gMenu.field_0x4 != row_idx) {
                SetMenuType(2);
                CreateDialogBox(8, 0);
            } else {
                SetFileSelectState(STATE_NONE);
            }
            break;
        case B_BUTTON:
            row_idx = gMenu.field_0x4;
            gSaveHeader->language = gMenu.field_0x4;
            SoundReq(SFX_MENU_CANCEL);
            SetFileSelectState(STATE_NONE);
            break;
    }

    // Lazy solution for positioning the rows lower on the screen
    if (row_idx < 2) {
        row_idx = 2;
    }
    if (row_idx > 6) {
        row_idx = 6;
    }
    if (gSaveHeader->language != row_idx) {
        gSaveHeader->language = row_idx;
        SoundReq(SFX_TEXTBOX_CHOICE);
    }
}

void sub_08050E88(void) {
    if (HandleSave(2))
        SetFileSelectState(0);
}

void sub_08050EB8(void);
void sub_08050EF4(void);
void sub_08050FFC(void);
void sub_0805103C(void);
void (*const gUnk_080FC950[])(void) = {
    sub_08050EB8,
    sub_08050EF4,
    sub_08050FFC,
    sub_0805103C,
};

void HandleFileOptions(void) {
    gUnk_080FC950[gMenu.menuType]();
}

void sub_08050EB8(void) {
    SaveFile* save;
    sub_080503A8(0xe);
    save = &gFileSelectState.saves[gFileSelectState.unk6];
    gFileSelectState.unk4 = save->msg_speed;
    gFileSelectState.unk5 = save->brightness;
    gMenu.column_idx = 0;
    gMenu.transitionTimer = 255;
    SetMenuType(1);
}

void sub_08050EF4(void) {
    u8* p_option;
    u32 option;
    char column_idx;
    u8 mode;

    if (gFileSelectState.isTransitioning == 0) {
        SaveFile* currentFile = &gFileSelectState.saves[gFileSelectState.unk6];
        column_idx = gMenu.column_idx;

        if (column_idx == 0) {
            p_option = &currentFile->msg_speed;
        } else {
            p_option = &currentFile->brightness;
        }

        option = *p_option;
        mode = 0;
        switch (gInput.newKeys) {
            case DPAD_UP:
                column_idx = 0;
                break;
            case DPAD_DOWN:
                column_idx = 1;
                break;
            case DPAD_LEFT:
                if (option != 0) {
                    option--;
                }
                break;
            case DPAD_RIGHT:
                if (option < 2) {
                    option++;
                }
                break;

            case A_BUTTON:
            case START_BUTTON:
                if (gFileSelectState.unk4 != currentFile->msg_speed ||
                    gFileSelectState.unk5 != currentFile->brightness) {
                    mode = 2;
                    break;
                }
            case B_BUTTON:
                mode = 3;
                break;
        }

        if (mode != 0) {
            switch (mode) {
                case 2:
                    CreateDialogBox(8, 0);
                    SoundReq(SFX_TEXTBOX_SELECT);
                    break;
                case 3:
                    currentFile->msg_speed = gFileSelectState.unk4;
                    currentFile->brightness = gFileSelectState.unk5;
                    SoundReq(SFX_MENU_CANCEL);
                    break;
            }
            SetMenuType(mode);
            SetActiveSave(gFileSelectState.unk6);
        } else if (gMenu.column_idx != column_idx) {
            gMenu.column_idx = column_idx;
            SoundReq(SFX_TEXTBOX_CHOICE);
        } else if (option != *p_option) {
            *p_option = option;
            LoadOptionsFromSave(gFileSelectState.unk6);
            SoundReq(SFX_TEXTBOX_CHOICE);
        }
    }
}

void sub_08050FFC(void) {
    switch (HandleSave(0)) {
        case SAVE_ERROR:
            gMenu.transitionTimer = 30;
            ResetSaveFile(gFileSelectState.unk6);
            CreateDialogBox(9, 0);
        case SAVE_OK:
            SetMenuType(3);
            break;
    }
}

void sub_0805103C(void) {
    // this gets optimized as a ldrb
    switch (gMenu.transitionTimer % 256) {
        case 0:
            if (!(gInput.newKeys & (A_BUTTON | START_BUTTON)))
                return;
        default:
            gMenu.transitionTimer--;
            break;
        case 255:
            SetFileSelectState(STATE_NONE);
            break;
    }
}

void sub_08051090(void);
void sub_080610B8(void);
void sub_0805138C(void);
void sub_0805144C(void);
void (*const gUnk_080FC960[])(void) = {
    sub_08051090,
    sub_080610B8,
    sub_0805138C,
    sub_0805144C,
};

void HandleFileNew(void) {
    gUnk_080FC960[gMenu.menuType]();
}

void sub_08051090(void) {
    sub_080503A8(8);
    sub_08050790();
    sub_0805070C();
    sub_08051458();
    gScreen.bg1.yOffset = 0xff;
    gScreen.bg2.yOffset = 0xff;
    SetMenuType(1);
}

const u8 gUnk_080FC970[] = {
    1, 2, 3, 5, 8, 0, 0, 0, 1, 2, 5, 8, 0, 0, 0, 0,
};
const u8 gUnk_080FC980[] = {
    0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 0, 0, 0,
};
const u8 gUnk_080FC9A0[] = {
    1, 3, 6, 8, 11, 0, 0, 0, 1, 4, 7, 11, 11, 0, 0, 0,
};

void sub_080610B8(void) {
    u8 uVar7;
    s32 uVar6;
    s32 tmp4;
    const u8* puVar3;
    const u8* puVar4;
    const u8* puVar5;
    s32 iVar4;
    u8 bVar2;
    u32 tmp1;
    u32 tmp3;
    u8 tmp2;
    u8 cVar1;
    s32 uVar8;
    s32 uVar5;

    if (gFileSelectState.isTransitioning != 0) {
        return;
    }
    uVar7 = 0;
    switch (gInput.newKeys) {
        default:
            uVar6 = 0;
            tmp4 = 0;
            switch (gInput.menuScrollKeys) {
                case DPAD_UP:
                    tmp4 = -1;
                    break;
                case DPAD_DOWN:
                    tmp4 = 1;
                    break;
                case DPAD_LEFT:
                    uVar6 = -1;
                    break;
                case DPAD_RIGHT:
                    uVar6 = 1;
                    break;
            }
            gGenericMenu.unk10.a[1] = (gGenericMenu.unk10.a[1] + tmp4 + 6) % 6;
            if ((tmp4 | uVar6) != 0) {
                SoundReq(SFX_TEXTBOX_NEXT);
            }
            if (uVar6 != 0) {
                if (gGenericMenu.unk10.a[1] != 5) {
                    gGenericMenu.unk10.a[0] = (gGenericMenu.unk10.a[0] + uVar6 + 0xd) % 0xd;
                    puVar4 = gUnk_080FC980;
                    if (gSaveHeader->language != 0) {
                        puVar4 += 0x10;
                    }
                    gGenericMenu.unk10.a[2] = puVar4[gGenericMenu.unk10.a[0]];
                } else {
                    if (gSaveHeader->language == 0) {
                        iVar4 = 5;
                    } else {
                        iVar4 = 4;
                    }
                    gGenericMenu.unk10.a[2] = (gGenericMenu.unk10.a[2] + uVar6 + iVar4) % iVar4;
                    if (gSaveHeader->language != 0) {
                        puVar5 = gUnk_080FC9A0 + 8;
                    } else {
                        puVar5 = gUnk_080FC9A0;
                    }
                    gGenericMenu.unk10.a[0] = puVar5[gGenericMenu.unk10.a[2]];
                }
            }
            break;
        case B_BUTTON:
            if (gGenericMenu.unk10.a[3] != 0) {
                uVar7 = 5;
            } else {
                sub_08051358();
            }
            break;
        case START_BUTTON:
            if (gSaveHeader->language == 0) {
                iVar4 = 4;
            } else {
                iVar4 = 3;
            }
            if ((gGenericMenu.unk10.a[1] == 5) && (iVar4 == gGenericMenu.unk10.a[2])) {
                uVar7 = 8;
            } else {
                sub_08051358();
            }
            break;
        case A_BUTTON:
            if (gGenericMenu.unk10.a[1] == 5) {
                puVar3 = gUnk_080FC970;
                tmp1 = gGenericMenu.unk10.a[2];
                if (gSaveHeader->language != 0) {
                    tmp1 += 8;
                }
                uVar7 = puVar3[tmp1];

            } else {
                uVar7 = 6;
            }
            break;
        case L_BUTTON:
            if (gSaveHeader->language == 0) {
                uVar7 = 4;
            } else {
                uVar7 = 1;
            }
            break;
        case R_BUTTON:
            if (gSaveHeader->language == 0) {
                uVar7 = 7;
            } else {
                uVar7 = 2;
            }
            break;
    }
    tmp3 = gMenu.column_idx;
    switch (uVar7) {
        case 1:
            tmp3 = 0;
            break;
        case 2:
            tmp3 = 1;
            break;
        case 3:
            tmp3 = 2;
            break;
        case 4:
            tmp3++;
            break;
        case 5:
            if (gGenericMenu.unk10.a[3] != 0) {
                gGenericMenu.unk10.a[3]--;
                gSave.name[gGenericMenu.unk10.a[3]] = 0;
                gSave.name[gGenericMenu.unk10.a[3] + 1] = 0;
                sub_08051574(0x6c);
            }
            break;
        case 7:
            sub_08051480(0);
            break;
        case 6:
            tmp2 = ((gBG3Buffer[gGenericMenu.unk10.a[0] * 2 + 0xc3 + gGenericMenu.unk10.a[1] * 0x40]) >> 1);
            sub_08051480(tmp2);
            break;
        case 8:
            uVar8 = 5;
            do {
                cVar1 = gSave.name[uVar8];
                if ((cVar1 != 0) && (cVar1 != 0x20)) {
                    break;
                }
                gSave.name[uVar8] = 0;
                uVar8--;
            } while (0 <= uVar8);
            if (gSave.name[0] != 0) {
                uVar7 = 2;
            } else {
                uVar7 = 3;
            }
            sub_08051574(0x6a);
            SetMenuType(uVar7);
    }
    if (gSaveHeader->language == 0) {
        iVar4 = 3;
    } else {
        iVar4 = 2;
    }
    tmp3 = (s32)(tmp3 + iVar4) % (iVar4);
    if (gMenu.column_idx != tmp3) {
        gMenu.column_idx = tmp3;
        sub_08051458();
        SoundReq(SFX_TEXTBOX_SWAP);
    }
}

void sub_08051358(void) {
    gGenericMenu.unk10.a[2] = gSaveHeader->language == 0 ? 4 : 3;

    if (gGenericMenu.unk10.a[0] != 0x0b || gGenericMenu.unk10.a[1] != 0x5) {
        gGenericMenu.unk10.a[1] = 0x5;
        gGenericMenu.unk10.a[0] = 0xb;
        SoundReq(SFX_TEXTBOX_NEXT);
    }
}

void sub_080513A8(void);
void sub_080513C0(void);
void sub_0805141C(void);
void (*const gUnk_080FC9B0[])(void) = {
    sub_080513A8,
    sub_080513C0,
    sub_0805141C,
};

void sub_0805138C(void) {
    gUnk_080FC9B0[gMenu.overlayType]();
}

void sub_080513A8(void) {
    CreateDialogBox(5, 0);
    gMenu.overlayType = 1;
}

void sub_080513C0(void) {
    switch (HandleSave(0)) {
        case 1:
            gFileSelectState.saveStatus[gFileSelectState.unk6] = 1;
#ifdef PC_PORT
            if (Port_RandoFileMenu_ShouldOpenForNewFile()) {
                SetFileSelectState(STATE_RANDOMIZER_CONFIG);
                break;
            }
#endif
            SetMenuType(3);
            break;
        case 0:
            break;
        case -1:
            ResetSaveFile(gFileSelectState.unk6);
            CreateDialogBox(6, 0);
            gMenu.transitionTimer = 30;
            gMenu.overlayType = 2;
            break;
    }
}

void sub_0805141C(void) {
    switch (gMenu.transitionTimer) {
        case 0:
            if (gInput.newKeys & (A_BUTTON | START_BUTTON)) {
                SetMenuType(3);
            }
            break;
        default:
            gMenu.transitionTimer--;
            break;
    }
}

void sub_0805144C(void) {
    SetFileSelectState(STATE_NONE);
}

void sub_08051458(void) {
    sub_080503A8(gMenu.column_idx + 9);
    MemCopy(&gBG3Buffer[0x80], &gBG1Buffer[0x80], 0x400);
}

u32 sub_080514BC(u32);

void sub_08051480(u32 c) {
    CharResult result;
    u32 idx;

    result = sub_080514BC(c);
    idx = gGenericMenu.unk10.a[3];

    if (result == RESULT_INVALID) {
        return;
    }

    if (result != RESULT_ASCII) {
        c = result;
        idx--;
    }

    if (idx > FILENAME_LENGTH - 1) {
        idx = FILENAME_LENGTH - 1;
    }

    gGenericMenu.unk10.a[3] = idx + 1;
    gSave.name[idx] = c;
    sub_08051574(SFX_6B);
}

u32 sub_080514BC(u32 a1) {
    u32 c;
    u32 idx;

    if (gSaveHeader->language != 0)
        return 1;

    switch (a1) {
        case 0x0:
        case 0xe:
        case 0xf:
            break;
        default:
            return 1;
    }

    idx = gGenericMenu.unk10.a[3];
    if (idx == 0)
        return 0;

    c = gSave.name[idx - 1];
    if (c - 0xa4 < 0x29) {
        switch (a1) {
            case 0x0:
                if (c < 0xc3) {
                    return c + 0x33;
                } else {
                    return c + 10;
                }
            case 0xE:
                return c;
            case 0xF:
                if (c < 0xc3)
                    return 0;
                else
                    return c + 10;
            default:
                return c;
        }
    }

    if (c - 0xcd < 10) {
        switch (a1) {
            case 0xf:
                return c;
            case 0x0:
                return c + 0x29;
            case 0xe:
                return c - 10;
        }
    }

    if (c - 0xd7 < 0x29) {
        switch (a1) {
            default:
            case 0x0:
            case 0xe:
                return c - 0x33;
            case 0xf:
                if (c > 0xf5)
                    return c - 0x29;
        }
    }

    return 0;
}

void sub_08051574(u32 sfx) {
    SoundReq(sfx);
    MemCopy(&gSave, &gFileSelectState.saves[gFileSelectState.unk6],
            sizeof(gFileSelectState.saves[gFileSelectState.unk6]));
    sub_0805070C();
}

void sub_080515C8(void);
void sub_080515D4(void);
void sub_080516E0(void);
void (*const gUnk_080FC9BC[])(void) = {
    sub_080515C8,
    sub_080515D4,
    sub_080516E0,
};

void HandleFileDelete(void) {
    gUnk_080FC9BC[gMenu.menuType]();
    sub_08050A64(gFileSelectState.unk6);
}

void sub_080515C8(void) {
    SetMenuType(1);
}

void sub_080515D4(void) {
    u32 column_idx;

    if (gFileSelectState.isTransitioning)
        return;

    gMenu.transitionTimer = 4;
    column_idx = gMenu.column_idx;
    switch (gInput.newKeys) {
        case B_BUTTON:
            gMenu.column_idx = 0; // ??
            column_idx = 0;
        case A_BUTTON:
        case START_BUTTON:
            if (column_idx == 1) {
                CreateDialogBox(4, 0);
                SetMenuType(2);
                SoundReq(SFX_TEXTBOX_SELECT);
            } else {
                SetFileSelectState(0);
                SoundReq(SFX_MENU_CANCEL);
            }
            break;
        case DPAD_LEFT:
            column_idx = 0;
            break;
        case DPAD_RIGHT:
            column_idx = 1;
            break;
    }

    if (gMenu.column_idx != column_idx) {
        gMenu.column_idx = column_idx;
        SoundReq(SFX_TEXTBOX_CHOICE);
    }
}

void sub_080516E0(void) {
    if (HandleSave(1)) {
        ResetSaveFile(gFileSelectState.unk6);
        sub_08050AFC(gFileSelectState.unk6);
        gMenu.transitionTimer = 2;
        SetFileSelectState(0);
    }
}

void sub_08051738(void);
void sub_080517EC(void);
void sub_08051874(void);
void sub_080518E4(void);
void (*const gUnk_080FC9C8[])(void) = {
    sub_08051738,
    sub_080517EC,
    sub_08051874,
    sub_080518E4,
};

void HandleFileCopy(void) {
    gUnk_080FC9C8[gMenu.menuType]();
    sub_08050A64(gFileSelectState.unk6);
}

void sub_08051738(void) {
    s32 temp;
    u32 i;
    s32 uVar3;
    s32 val;
    u32 temp2;

    gFileSelectState.unk7 = 4;
    uVar3 = 0;
    for (i = 0; i < 3; i++) {
        if (gFileSelectState.saveStatus[i] == 1) {
            temp = gFileSelectState.unk6;
            val = 4;
            if ((temp ^ i) == 0) {
                val = 0;
            }
        } else {
            uVar3++;
            val = uVar3;
        }
        gGenericMenu.unk10.a[i] = val;
    }
    if (uVar3 == 0) {
        gMenu.transitionTimer = 60;
        CreateDialogBox(1, 0);
        SetMenuType(3);
    } else {
        SetMenuType(1);
    }
    uVar3++;
    gGenericMenu.unk16 = uVar3;
    gGenericMenu.unk10.a[3] = 4;
    gGenericMenu.unk14 = uVar3;
}

s32 sub_080517B4(s32 a1) {
    u32 i = gFileSelectState.unk7;
    if (a1 != 0) {
        for (i = i + a1; i < 5; i += a1) {
            if (gGenericMenu.unk10.a[i] != 0 && gGenericMenu.unk10.a[i] != 4)
                return i;
        }

        i = gFileSelectState.unk7;
    }
    return i;
}

void sub_080517EC(void) {
    u32 temp;
    s32 delta;

    if (gFileSelectState.isTransitioning)
        return;

    delta = 0;
    switch (gInput.newKeys) {
        case DPAD_UP:
            delta = -1;
            break;
        case DPAD_DOWN:
            delta = 1;
            break;
        case A_BUTTON:
        case START_BUTTON:
            if (gFileSelectState.unk7 < 3) {
                CreateDialogBox(2, 0);
                SetMenuType(2);
                SoundReq(SFX_TEXTBOX_SELECT);
                break;
            }
            // fallthrough
        case B_BUTTON:
            gFileSelectState.unk7 = 4;
            SoundReq(SFX_MENU_CANCEL);
            SetFileSelectState(0);
            break;
    }
    temp = sub_080517B4(delta);
    if (temp != gFileSelectState.unk7) {
        gFileSelectState.unk7 = temp;
        SoundReq(SFX_TEXTBOX_CHOICE);
    }
}

void sub_08051874(void) {
    s32 temp;
    gSaveHeader->saveFileId = gFileSelectState.unk7;
    temp = HandleSave(0);
    gFileSelectState.saveStatus[gFileSelectState.unk7] = temp;
    switch (temp) {
        case 1:
            MemCopy(&gSave, &gFileSelectState.saves[gFileSelectState.unk7],
                    sizeof(gFileSelectState.saves[gFileSelectState.unk7]));
#ifdef PC_PORT
            {
                extern void Port_RandoSave_CopySlot(int src, int dst);
                Port_RandoSave_CopySlot((int)gFileSelectState.unk6, (int)gFileSelectState.unk7);
            }
#endif
            SetFileSelectState(0);
            break;
        case -1:
            ResetSaveFile(gFileSelectState.unk7);
            CreateDialogBox(3, 0);
            gMenu.transitionTimer = 30;
            SetMenuType(3);
            break;
    }
}

void sub_080518E4(void) {
    switch (gMenu.transitionTimer) {
        case 0:
            if (gInput.newKeys & (A_BUTTON | START_BUTTON)) {
                SetFileSelectState(0);
            }
            break;
        default:
            gMenu.transitionTimer--;
            break;
    }
}

#ifdef PC_PORT
static void HandleFileRandoConfig(void) {
    if (!Port_RandoFileMenu_IsOpen()) {
        Port_RandoFileMenu_Open((int)gFileSelectState.unk6);
    }
}
#endif

void HandleFileStart(void) {
    if (gMenu.menuType == 0) {
        gMenu.menuType = 1;
        gSaveHeader->msg_speed = gSave.msg_speed;
        gSaveHeader->brightness = gSave.brightness;
        gMain.state = GAMETASK_MAIN;
        SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
    }
}

void ResetSaveFile(u32 save_idx) {
    SaveFile* save;

    gFileSelectState.saveStatus[save_idx] = 0;
#ifdef PC_PORT
    {
        extern void Port_RandoSave_ClearSlot(int slot);
        Port_RandoSave_ClearSlot((int)save_idx);
    }
#endif
    save = &gFileSelectState.saves[save_idx];
    MemClear(save, sizeof(SaveFile));
    save->msg_speed = 1;
    save->brightness = 1;
    save->stats.health = 24;
    save->stats.maxHealth = 24;
}
