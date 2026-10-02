#ifndef REGION_H
#define REGION_H

/*
 * region.h — region identity for the multi-region PC port.
 *
 * The Minish Cap differs by region (USA / EU / JP) in gameplay logic, data, and
 * text. Historically those differences are compile-time `#ifdef EU` / `#ifdef JP`
 * blocks, which forces one binary per region. This header lets the SAME source
 * serve two masters:
 *
 *   - DEFAULT (the byte-matching GBA ROM build, or a single-region PC build):
 *     REGION_IS_* are COMPILE-TIME CONSTANTS picked by the -DUSA/-DEU/-DJP define.
 *     Dead branches fold away and the decomp still byte-matches the retail ROM.
 *
 *   - PC_PORT + MULTI_REGION (the fat binary): REGION_IS_* read the runtime
 *     `gActiveRegion`, set from the loaded ROM at boot. One binary plays any ROM.
 *
 * Convert a decomp site mechanically, preprocessor -> C control flow:
 *
 *     #ifdef EU            ->    if (REGION_IS_EU) {
 *         A                          A
 *     #else                ->    } else {
 *         B                          B
 *     #endif               ->    }
 *
 *     #if defined(JP) || defined(EU)   ->   if (REGION_IS_JP || REGION_IS_EU)
 *     #ifndef EU                       ->   if (!REGION_IS_EU)
 *
 * In MULTI_REGION BOTH branches are compiled, so any symbol referenced by a
 * non-active branch must exist for all regions — version-exclusive functions
 * (e.g. AreaAllowsWarp, USA-only) get un-#ifdef'd definitions so they always link.
 *
 * This header is force-included by the build, so it is available everywhere
 * without per-file #includes. In DEFAULT mode it expands to nothing but macros,
 * so it cannot perturb the matching build.
 */

#define TMC_REGION_USA 0
#define TMC_REGION_EU  1
#define TMC_REGION_JP  2

#if defined(PC_PORT) && defined(MULTI_REGION)

#ifdef __cplusplus
extern "C" {
#endif
/* One of TMC_REGION_*. Set once at ROM load (Port_Region_SetActive), from the
 * detected ROM region. Read by the REGION_IS_* macros below. */
extern int gActiveRegion;
#ifdef __cplusplus
}
#endif

#define REGION_IS_USA (gActiveRegion == TMC_REGION_USA)
#define REGION_IS_EU  (gActiveRegion == TMC_REGION_EU)
#define REGION_IS_JP  (gActiveRegion == TMC_REGION_JP)

#else /* compile-time constant — preserves byte-matching */

#if defined(EU)
#define REGION_IS_USA 0
#define REGION_IS_EU  1
#define REGION_IS_JP  0
#elif defined(JP)
#define REGION_IS_USA 0
#define REGION_IS_EU  0
#define REGION_IS_JP  1
#else /* USA / DEMO_USA / default */
#define REGION_IS_USA 1
#define REGION_IS_EU  0
#define REGION_IS_JP  0
#endif

#endif

/*
 * Subdirectory name for the active region's extracted asset cache.
 *
 * The asset cache is keyed per-region (assets/<sub>/, assets_src/<sub>/) so that
 * switching ROMs never reuses or overwrites another region's extracted data —
 * the same isolation the save files use (tmc.sav / tmc_eu.sav / tmc_jp.sav). All
 * three retail ROMs are 16 MB, so the runtime up-to-date fingerprint cannot tell
 * them apart by size; a dedicated folder per region removes the ambiguity.
 *
 * Works in both modes: REGION_IS_* are runtime reads in MULTI_REGION and folded
 * constants otherwise. UNKNOWN falls through to "usa" (the asset baseline).
 *
 * Guarded to PC_PORT so the byte-matching GBA build stays pure-macro (see above).
 */
#ifdef PC_PORT
static inline const char* RegionAssetSubdir(void) {
    if (REGION_IS_EU) return "eu";
    if (REGION_IS_JP) return "jp";
    return "usa";
}
#endif

#endif /* REGION_H */
