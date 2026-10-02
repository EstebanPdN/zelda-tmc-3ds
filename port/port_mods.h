#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mod loader (Tier 1: asset overrides).
 *
 * Discovers mod directories and registers them as override roots that
 * the asset loader checks BEFORE pak archives and the loose tree. Each
 * mod is a directory whose contents mirror the runtime `assets/` tree:
 *
 *   mods/
 *     widescreen-tilemaps/
 *       tilemaps/
 *         tilemap_0042.bin       ← overrides assets/tilemaps/tilemap_0042.bin
 *     buttons-xbox/
 *       gfx/
 *         gfx_12345_64x64_4bpp_uncompressed.bin
 *
 * Discovery order:
 *   1. TMC_MODS env var: comma-separated list, e.g.
 *        TMC_MODS=buttons-xbox,widescreen-tilemaps
 *      Each entry is a directory name under a discovered mods/ root.
 *      Only listed mods are active; first entry wins on file collision
 *      (left-to-right priority).
 *   2. Otherwise, the asset loader auto-discovers every directory
 *      directly inside each asset search root's mods/ directory, sorted
 *      alphabetically (deterministic but no priority control).
 *
 * The loader checks both binary-local and cwd-local mods/ during explicit
 * TMC_MODS resolution. No mods are loaded if no active mods/ root exists
 * or every selected name is missing.
 *
 * Mods can override any asset path the runtime asks for via
 * LoadBinaryFileCached — gfx, palettes, animations, tilemaps, room
 * properties, area tables, text strings, ... Anything that ends up in
 * one of the *.pak archives is moddable.
 */
void Port_Mods_Init(void);

#ifdef __cplusplus
}
#endif
