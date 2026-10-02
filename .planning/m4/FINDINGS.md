# M4 classification — key findings (in progress)

Generated from the `m4-classify-remaining` workflows (one agent per file, structured
classification + adversarial reachability verification of data-table baseline-safe claims).

## Category legend
- **convert-now** — function-local control flow; mechanical `#ifdef` → `if (REGION_IS_*)`. Safe.
- **just-unguard** (EXCLUSIVE_FN + callerAlreadyBranched=yes) — remove the `#ifdef` so the
  region-exclusive function always compiles; the call site already runtime-branches. Safe.
- **drop-demo-arm** — `#if defined(DEMO_*)` whose `#else` is the live retail (often already
  runtime) path; DEMO is not a runtime region, so keep `#else` unconditionally. Safe.
- **per-region-table** — static data the non-baseline region DOES observe; needs two tables +
  runtime select (or a region-indexed override). The TRAP category.
- **needs-fn-dispatch** — region-exclusive function whose caller is NOT region-branched, or
  whose variants have different signatures; needs both compiled + a runtime dispatcher.
- **restructure-case** — case-label guard; needs control-flow restructuring.
- **defer-menu / defer-core** — title/menu layout, or boot/save core files.

## FOUNDATIONAL BLOCKER — flags.h LocalFlags1 enum divergence (defer, needs design)

`include/flags.h` `LocalFlags1` (and likely other flag banks) has **wholesale per-region
divergence**: USA vs EU/JP have different *sets and counts* of enumerators (e.g. USA has
`KS_A06/KS_B18/KS_C21/KS_C25`; EU/JP have `MIZUUMI_00_CAP_0, SUIGEN_00_R2, YAMA_04_CAP_1,
LOST_05_02/03, KAKERA_TAKARA_J`, plus inserts like `HIKYOU_00_H00`, `SOUGEN_07_H00`). Because
a flag's save-bit position is computed as `offset + enum_ordinal` (`flags.c:CheckLocalFlagByBank`
→ `CheckBits(gSave.flags, offset + flag, count)`), every flag after a divergence point sits at a
**different bit ordinal** in EU/JP than in USA.

**Why it matters in the fat binary (USA baseline):** the compiled C code references flags by
USA ordinals, but a loaded EU/JP ROM's *scripts and area-data* reference flags by that region's
ordinals. Any logical flag touched by BOTH a script and C code desyncs on EU/JP (writer and
reader hit different bits). Self-consistent for purely-C or purely-script flags, but not for
mixed ones — and not compatible with real hardware EU/JP save files either.

**Severity:** real EU/JP correctness/save-layout issue, blast radius = the entire local-flag
subsystem. **Not** a mechanical conversion. Requires a region-indexed flag-ordinal remap (per-
region ordinal table selected by `gActiveRegion`) or per-region enum compiled both ways + select.
Connects to the known "saves region-coupled (ZELDA5 USA vs ZELDA3 EU/JP signature)" item.
**Recommendation: DEFER to a dedicated save/flag-layout phase.** Do NOT attempt mechanically.

## Confirmed per-region DATA tables the non-baseline region OBSERVES (need per-region-table)
- `itemDefinitions.c:21` `gItemDefinitions` — lantern (`ITEM_LANTERN_OFF/ON`) `animPriority`
  EU=6 vs USA/JP=2; read every region via `playerUtils.c` AssignBehavior→animPriority→max-priority
  animation select; EU observes wrong value. (single field, 2 entries)
- `vaatiRebornEnemy.c:89` `gUnk_080D04D0` — Vaati Reborn per-phase damage thresholds EU
  {-12,-20,-32} vs USA/JP {-24,-40,-48}; read region-shared at `:716`; EU observes. (3 bytes)
- `dust.c:43`, `vaatiTransfigured.c:100` — sprite-index `#define`/array (per earlier wave flags).
- objectDefinitions.c (51 sites), sound.c gSongTable, itemMetaData.c, npcDefinitions.c,
  data/*.c — TBD by classification batch 2.

## Easy-win sites already identified (batch 1)
- CONTROL_FLOW_CONVERTIBLE convert-now: `vaatiArm.c:587,1492`; `gyorgFemaleEye.c:87/91/104`
  (interleaved switch — handle carefully); `vaatiWrath.c:145`; `title.c:432` (JP/EU=360 vs USA=300);
  `gameOverTask.c:68,84` (drop-demo-arm, take #else); numerous `title.c` DEMO_JP arms (drop, keep #else).
- just-unguard EXCLUSIVE_FN (caller already branched): `enemy50.c:427` (sub_08041300),
  `gyorgFemale.c:275` (GyorgFemale_OnEnterRoom).
- unguard + call-gate EXCLUSIVE_FN: `vaatiWrath.c` VaatiWrathType0PreAction (def @1095, call @145,
  include @24) — USA-only; gate call with `if (REGION_IS_USA)`, always-compile def+include.
- baseline-safe (leave / drop): `title.c:82` gDemoSave (DEMO-only, verified baseline-safe).
