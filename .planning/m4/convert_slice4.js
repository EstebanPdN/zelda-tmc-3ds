export const meta = {
  name: 'm4-slice4-convert',
  description: 'Convert M4 slice-4 per-region static-data sites to runtime region select (one agent per file)',
  phases: [
    { title: 'Convert', detail: 'one agent per file: classify + apply runtime region select' },
  ],
}

// Work list: file -> divergent line numbers (from .planning/m4/PLAN.md SLICE 4).
// Excludes flags.h (deferred ordinal blocker), slice-5 files, and already-done files
// (objectDefinitions.c, vaatiRebornEnemy.c, vaatiTransfigured.c, itemDefinitions.c).
const FILES = [
  ['include/main.h', [31]],
  ['src/collision.c', [1031,1038,1055,1062,1079,1087,1103,1110,1127,1135,1546,1553,2250,2258,2264]],
  ['src/common.c', [2100,2145,2236]],
  ['src/data/figurineMenuData.c', [47]],
  ['src/data/objPalettes.c', [125]],
  ['src/data/screenTransitions.c', [33,63]],
  ['src/data/transitions.c', [523,2616,2623,2630,2637,2652]],
  ['src/droptables.c', [515,787]],
  ['src/enemy.c', [58,68,91,111,151,169,199,255]],
  ['src/enemy/bombPeahat.c', [765]],
  ['src/enemy/businessScrub.c', [643]],
  ['src/fileselect.c', [72,89,97,144,167]],
  ['src/gameData.c', [156,160,167,195,212,255,377,386]],
  ['src/itemMetaData.c', [170,235,243]],
  ['src/main.c', [217]],
  ['src/manager/minishVillageTileSetManager.c', [25,37]],
  ['src/manager/miscManager.c', [64,67]],
  ['src/menu/figurineMenu.c', [323]],
  ['src/menu/pauseMenu.c', [531,1239]],
  ['src/npc/dog.c', [44]],
  ['src/npc/forestMinish.c', [219]],
  ['src/npc/goronMerchant.c', [214]],
  ['src/npc/kid.c', [174,188]],
  ['src/npc/npc4E.c', [269]],
  ['src/npc/pita.c', [26]],
  ['src/npcDefinitions.c', [34,148,270,279,285]],
  ['src/object/cameraTarget.c', [27]],
  ['src/object/figurineDevice.c', [586]],
  ['src/object/graveyardKey.c', [56]],
  ['src/object/minishPortalCloseup.c', [45]],
  ['src/object/specialFx.c', [123]],
  ['src/projectile.c', [39,56,119,164]],
  ['src/sound.c', [1020,1115]],
  ['src/worldEvent/worldEvent17.c', [64]],
]

const SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['file','applied','sitesConverted','sitesSkipped','externalEdits','externSymbolsAdded','buildRisk','notes'],
  properties: {
    file: { type: 'string' },
    applied: { type: 'boolean', description: 'true if you wrote changes to your assigned file' },
    mechanism: { type: 'string' },
    sitesConverted: { type: 'integer' },
    sitesSkipped: { type: 'integer', description: 'sites left as-is because genuinely baseline-safe / not observed' },
    externSymbolsAdded: { type: 'array', items: { type: 'string' }, description: 'new *_eu global symbols you added (so consumers in other files can extern them)' },
    externalEdits: {
      type: 'array',
      description: 'edits required in files OTHER than your assigned file (consumers). DO NOT apply these yourself.',
      items: {
        type: 'object', additionalProperties: false,
        required: ['path','oldString','newString','reason'],
        properties: {
          path: { type: 'string' },
          oldString: { type: 'string', description: 'exact unique snippet to replace' },
          newString: { type: 'string' },
          reason: { type: 'string' },
        },
      },
    },
    buildRisk: { type: 'string', description: 'anything that might break the build, or "none"' },
    notes: { type: 'string' },
  },
}

const RECIPE = `
You are converting Minish Cap decomp compile-time region guards into RUNTIME region
selection for the multi-region "fat binary" (single PC binary that loads USA/EU/JP ROMs
at runtime). Baseline assets/tables are USA; EU and JP ROMs are loaded at runtime.

CONTEXT YOU MUST INTERNALIZE:
- The macro MULTI_REGION is defined for this build. region.h is force-included, so
  REGION_IS_USA / REGION_IS_EU / REGION_IS_JP are available everywhere as runtime checks.
- A compile-time guard like "#ifdef EU ... #else ... #endif" currently compiles only ONE
  arm. In the fat binary (USA baseline, so EU/JP are NOT defined) only the USA/non-EU arm
  compiles, so a loaded EU/JP ROM observes the WRONG static data. Your job: make BOTH
  values available and select at runtime by region.
- Native single-region builds (no MULTI_REGION) must keep working unchanged.

THREE MECHANISMS (pick the lightest that fits the site):

(A) runtime-override — for a SINGLE diverging field/value read at ONE (or few) consumer
    site(s), especially when the data is 'const' so it can't be patched. Leave the data
    table exactly as-is (its existing #ifdef stays; in the fat binary it holds the USA
    baseline). At the consumer, after the baseline read, override for the relevant region:
        x = table[i].field;
        #ifdef MULTI_REGION
        if (REGION_IS_EU /* and/or JP, matching the ORIGINAL guard */) { x = <eu value>; }
        #endif
    Use this when the consumer is in the SAME file. If the consumer is in ANOTHER file,
    DO NOT edit that file — return it in externalEdits.

(B) twin-select — for a small whole table/array that diverges. Keep the canonical symbol
    as the baseline (USA/JP), but make native-EU still correct, and add an _eu twin:
        #if defined(EU) && !defined(MULTI_REGION)
        const T name[] = { ...EU body... };
        #else
        const T name[] = { ...USA/JP body... };
        #endif
        #ifdef MULTI_REGION
        const T name##_eu[] = { ...EU body... };   // literally name_eu
        #endif
    Then at each reader, select:
        const T* sel = name;
        #ifdef MULTI_REGION
        if (REGION_IS_EU) sel = name_eu;
        #endif
        ... use sel[i] ...
    (If the original guard also covers JP, e.g. "#if defined(JP) || defined(EU)", then the
     EU body is really the JP+EU body; select with "if (REGION_IS_EU || REGION_IS_JP)".)

(C) fn-dispatch / unguard — if the site is a region-exclusive FUNCTION (e.g. a name ending
    _USA, or "#ifdef EU"-guarded function def) rather than data: make it always compile by
    changing its guard to "#if ... || defined(MULTI_REGION)", and runtime-gate the CALL site
    with the matching REGION_IS_*. (Same idiom slices 1-3 used.)

CRITICAL RULES:
- PRESERVE THE EXACT REGION CONDITION. Translate the literal preprocessor condition to the
  equivalent runtime expression. "#ifdef EU" -> REGION_IS_EU. "#ifndef EU" -> !REGION_IS_EU.
  "#if defined(JP)||defined(EU)" -> (REGION_IS_JP||REGION_IS_EU). Note baseline is USA, so
  whichever arm USA falls into is the canonical/baseline body.
- Gate ALL new runtime machinery on "#ifdef MULTI_REGION". Never reference _eu symbols or
  REGION_IS_* outside a MULTI_REGION guard in a way that would break single-region builds.
- Edit ONLY your assigned file. For any consumer in a DIFFERENT file, return an externalEdit
  (exact oldString/newString); do NOT open or write that file's changes.
- VERIFY the site is genuinely observed by the non-baseline region. If after reading the code
  you conclude a site is truly baseline-safe (non-baseline region never reads it — e.g. a
  debug-name table, a DEMO-only arm), SKIP it (leave as-is) and count it in sitesSkipped with
  a note. Do not convert speculatively.
- Do NOT run the build, do NOT run git. Keep the decomp's formatting/style.
- Values must be byte-exact. Double-check signedness and hex.

Use Read to inspect your file and any consumers (grep for the symbol across src/ and include/).
Apply your edits to the assigned file with Edit/Write. Return the structured manifest.
`

phase('Convert')
const results = await parallel(FILES.map(([file, lines]) => () =>
  agent(
    `${RECIPE}\n\nYOUR ASSIGNED FILE: ${file}\nDivergent line numbers (approximate; verify by reading): ${lines.join(', ')}\n\n` +
    `Convert every genuinely-observed per-region site in this file. Return the manifest.`,
    { label: file.replace(/^src\//,''), phase: 'Convert', schema: SCHEMA }
  ).then(r => r || { file, applied: false, sitesConverted: 0, sitesSkipped: 0, externalEdits: [], externSymbolsAdded: [], buildRisk: 'agent returned null', notes: 'agent died' })
))

log(`Converted ${results.filter(r=>r&&r.applied).length}/${FILES.length} files; ` +
    `${results.reduce((a,r)=>a+(r?.externalEdits?.length||0),0)} external (cross-file) edits pending`)
return results
