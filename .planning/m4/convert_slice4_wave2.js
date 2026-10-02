export const meta = {
  name: 'm4-slice4-convert-wave2',
  description: 'M4 slice-4 wave 2: convert remaining per-region data sites (rate-limited + reverted files)',
  phases: [
    { title: 'Convert', detail: 'one agent per remaining file: classify + apply runtime region select, audit every consumer' },
  ],
}

// Remaining files: wave-1 rate-limited agents + 3 reverted (botched/partial).
const FILES = [
  ['include/main.h', [31]],
  ['src/data/screenTransitions.c', [33,63]],
  ['src/data/transitions.c', [523,2616,2623,2630,2637,2652]],
  ['src/enemy.c', [58,68,91,111,151,169,199,255]],
  ['src/enemy/businessScrub.c', [643]],
  ['src/fileselect.c', [72,89,97,144,167]],
  ['src/gameData.c', [156,160,167,195,212,255,377,386]],
  ['src/itemMetaData.c', [170,235,243]],
  ['src/manager/miscManager.c', [64,67]],
  ['src/manager/minishVillageTileSetManager.c', [25,37]],
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
  required: ['file','applied','sitesConverted','sitesSkipped','externalEdits','externSymbolsAdded','consumerAudit','buildRisk','notes'],
  properties: {
    file: { type: 'string' },
    applied: { type: 'boolean' },
    mechanism: { type: 'string' },
    sitesConverted: { type: 'integer' },
    sitesSkipped: { type: 'integer' },
    externSymbolsAdded: { type: 'array', items: { type: 'string' } },
    consumerAudit: {
      type: 'array',
      description: 'EVERY reader of every base symbol you twinned, with its disposition.',
      items: {
        type: 'object', additionalProperties: false,
        required: ['symbol','readerLocation','disposition'],
        properties: {
          symbol: { type: 'string' },
          readerLocation: { type: 'string', description: 'file:line' },
          disposition: { type: 'string', enum: ['wired-in-file','wired-external','baseline-safe'], },
          justification: { type: 'string', description: 'required if baseline-safe: why the non-baseline region produces an identical observable result' },
        },
      },
    },
    externalEdits: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        required: ['path','oldString','newString','reason'],
        properties: { path:{type:'string'}, oldString:{type:'string'}, newString:{type:'string'}, reason:{type:'string'} },
      },
    },
    buildRisk: { type: 'string' },
    notes: { type: 'string' },
  },
}

const RECIPE = `
You convert Minish Cap decomp compile-time region guards into RUNTIME region selection for
the multi-region "fat binary" (one PC binary loading USA/EU/JP ROMs at runtime; baseline
tables are USA, so EU/JP ROMs would otherwise read wrong static data).

MULTI_REGION is defined; region.h is force-included so REGION_IS_USA/REGION_IS_EU/REGION_IS_JP
are available everywhere as runtime checks. Native single-region builds (no MULTI_REGION) must
keep working unchanged.

MECHANISMS (pick the lightest that fits):
(A) runtime-override — a single diverging field/value read at one/few consumers, esp. const
    data. Leave the table's existing #ifdef as-is (fat binary holds the USA baseline). At each
    consumer:  x = table[i].field;
               #ifdef MULTI_REGION
               if (REGION_IS_EU /* match original guard's region set */) x = <other-region value>;
               #endif
(B) twin-select — a small whole table/array diverges. Keep canonical = baseline but keep native
    EU correct, and add an _eu (and _jp if the guard distinguishes JP) twin:
        #if defined(EU) && !defined(MULTI_REGION)
        const T name[] = { ...EU... };
        #else
        const T name[] = { ...USA/JP baseline... };
        #endif
        #ifdef MULTI_REGION
        const T name_eu[] = { ...EU... };
        #endif
    At EACH reader, default to baseline then override:
        const T* sel = name;
        #ifdef MULTI_REGION
        if (REGION_IS_EU) sel = name_eu;
        #endif
        ...use sel[i]...
(C) fn-dispatch / unguard — region-exclusive FUNCTION: make it always compile
    ("#if ... || defined(MULTI_REGION)") and runtime-gate the CALL site.

==== TWO MISTAKES FROM WAVE 1 — DO NOT REPEAT ====
1. NEVER assign a twin pointer UNCONDITIONALLY inside #ifdef MULTI_REGION. This is WRONG:
        #ifdef MULTI_REGION
            sel = name_eu;       // BUG: USA/JP now read the EU table!
        #else
            sel = name;
        #endif
   ALWAYS default to baseline and override only under "if (REGION_IS_EU)".
2. You MUST wire EVERY consumer of any symbol you twin — SAME-FILE consumers (edit directly)
   AND OTHER-FILE consumers (return as externalEdit). Before finishing, GREP THE WHOLE REPO
   ("grep -rn <baseSymbol> src/ include/ port/") for every base symbol you twinned and account
   for EVERY reader in the consumerAudit array. A reader is only 'baseline-safe' if the
   non-baseline region yields an identical OBSERVABLE result (e.g. the read feeds only an
   equality test against constants that none of the differing values equal) — justify it.

CRITICAL RULES:
- PRESERVE THE EXACT REGION CONDITION. "#ifdef EU"->REGION_IS_EU; "#ifndef EU"->!REGION_IS_EU;
  "#if defined(JP)||defined(EU)"->(REGION_IS_JP||REGION_IS_EU). Baseline is USA.
- Gate ALL new runtime machinery on "#ifdef MULTI_REGION". Never reference _eu/_jp symbols or
  REGION_IS_* outside MULTI_REGION in a way that breaks single-region builds.
- Edit ONLY your assigned file directly. Cross-file consumers -> externalEdits (exact unique
  oldString/newString). Do not edit other files.
- VERIFY each site is genuinely observed by the non-baseline region; if truly baseline-safe
  (debug-name table, DEMO-only arm), SKIP it and count in sitesSkipped with a note.
- Values byte-exact; mind signedness/hex. Keep decomp formatting. Do NOT build or run git.
- Finish your edits in one pass so the file is never left half-converted.

Use Read + grep to inspect your file and all consumers. Apply edits to your assigned file with
Edit/Write. Return the structured manifest INCLUDING the full consumerAudit.
`

phase('Convert')
const results = await parallel(FILES.map(([file, lines]) => () =>
  agent(
    `${RECIPE}\n\nYOUR ASSIGNED FILE: ${file}\nDivergent line numbers (approximate; verify by reading): ${lines.join(', ')}\n\n` +
    `Convert every genuinely-observed per-region site in this file, wire EVERY consumer, and return the manifest with consumerAudit.`,
    { label: file.replace(/^src\//,''), phase: 'Convert', schema: SCHEMA }
  ).then(r => r || { file, applied: false, sitesConverted: 0, sitesSkipped: 0, externalEdits: [], externSymbolsAdded: [], consumerAudit: [], buildRisk: 'agent returned null', notes: 'agent died' })
))

log(`Wave2: converted ${results.filter(r=>r&&r.applied).length}/${FILES.length}; ` +
    `${results.reduce((a,r)=>a+(r?.externalEdits?.length||0),0)} external edits pending`)
return results
