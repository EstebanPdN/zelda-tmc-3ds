/**
 * Per-region runtime remap for compiled USA-baseline blob offsets (M7
 * multi-region fix). Generated implementation: port/port_offset_remap.c
 * (tools/gen_offset_remap.py; regeneration needs extracted assets for all
 * three regions under dist/ or build/ — the committed .c is authoritative).
 *
 * Contract: apply ONLY to offsets that came from a compiled C table or
 * macro (offset_* from port_offset_USA.h / assets/gfx_offsets.h baked at
 * compile time). Offsets read out of ROM-native or asset-extracted data are
 * already region-correct — translating them would corrupt the lookup the
 * same way the untranslated compiled ones are corrupted today.
 *
 *   Port_RemapMapOffset — indices into gMapData          (dungeon maps,
 *                         cave borders, gyorg mappings)
 *   Port_RemapGfxOffset — indices into gGlobalGfxAndPalettes (obj palettes,
 *                         Hyrule Town tilesets). JP == USA for this blob.
 *
 * Identity on USA, for non-divergent offsets, and on non-multi-region builds.
 */
#ifndef PORT_OFFSET_REMAP_H
#define PORT_OFFSET_REMAP_H

#include "global.h"

#if defined(PC_PORT) && defined(MULTI_REGION)
u32 Port_RemapMapOffset(u32 usa_offset);
u32 Port_RemapGfxOffset(u32 usa_offset);
#else
#define Port_RemapMapOffset(off) (off)
#define Port_RemapGfxOffset(off) (off)
#endif

#endif /* PORT_OFFSET_REMAP_H */
