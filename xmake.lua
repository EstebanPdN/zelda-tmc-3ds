-- Native host regressions for the Nintendo 3DS port.
-- The installable application is built by platform/3ds/build.sh.
set_project("tmc-3ds-regressions")
set_xmakever("2.7.0")
add_rules("mode.debug", "mode.release")
set_defaultmode("release")
option("pc_sanitize")
    set_default(false)
    set_description("Enable AddressSanitizer and UndefinedBehaviorSanitizer in supported host tests")
option_end()
if is_host("linux") and os.getenv("XMAKE_USE_SYSTEM_SDL3") then
    add_requires("libsdl3", {system = true})
else
    add_requires("libsdl3 3.4.12", {configs = {shared = false}})
end

target("rng_golden_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port")
    add_files("tools/tests/port/port_rng_golden_test.c")
target_end()


-- ====================
-- Debug-menu action-layer regression test (guards per-item toggle exclusivity
-- + flag/dungeon/stat/bottle helpers; see port_debug_actions_test.c)
-- ====================
target("debug_actions_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT")
    add_files("src/port/port_debug_actions.c")
    add_files("tools/tests/port/port_debug_actions_test.c")
target_end()


-- ====================
-- GBA/native address-resolution regression test.
-- ====================
target("gba_mem_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT")
    add_files("src/port/port_gba_mem.c")
    add_files("tools/tests/port/port_gba_mem_test.c")
target_end()


-- ====================
-- Transition preserved-coordinate regression test.
-- ====================
target("transition_axis_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_files("tools/tests/port/port_transition_axis_test.c")
target_end()


-- ====================
-- Deepwood rolling-barrel doorway HDMA lifecycle regression test.
-- ====================
target("rolling_barrel_transition_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_includedirs("src/offsets/USA")
    add_defines("PC_PORT", "USA", "ENGLISH", "PORT_ROLLING_BARREL_TRANSITION_TEST")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_rolling_barrel_transition_test.c")
    add_files("src/port/port_hdma.c")
    add_files("src/engine/src/common.c")
    add_files("src/engine/src/manager/rollingBarrelManager.c")
target_end()


-- ====================
-- Multi-region language-slot regression test.
-- ====================
target("region_language_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("tools/tests/port/port_region_language_test.c")
target_end()


-- ====================
-- Multi-region fixed-UI and collision-pointer regression test.
-- ====================
target("region_runtime_data_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("tools/tests/port/port_region_runtime_data_test.c")
    add_files("src/port/port_offset_remap.c")
target_end()


-- ====================
-- Retail Object70 doorway/swamp head-overlay priority regression test.
-- ====================
target("object70_overlay_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_object70_overlay_test.c")
    add_files("src/engine/src/object/object70.c")
target_end()


-- ====================
-- EU sprite-table hole + byte-exact production OAM regression test.
-- ====================
target("bottle_compat_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("src/port/port_bottle_compat.c", "tools/tests/port/port_bottle_compat_test.c")
target_end()

target("chest_reward_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_chest_reward_test.c", "src/port/port_bottle_compat.c")
    add_files("src/engine/src/object/chestSpawner.c", "src/engine/src/playerItemUtils.c")
target_end()

target("sprite_region_oam_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_includedirs("src/port/ppu/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_sprite_region_oam_test.c")
    add_files("src/port/port_draw.c")
    add_files("src/engine/src/enemyUtils.c")
    add_files("src/engine/src/projectileUtils.c")
target_end()


-- ====================
-- Horizontal Minish-path GBA EWRAM-alias materialization regression test.
-- ====================
target("horizontal_minish_path_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_horizontal_minish_path_test.c")
    add_files("src/engine/src/manager/horizontalMinishPathBackgroundManager.c")
target_end()


-- ====================
-- NPC graphics-allocation failure/retry regression test (Goron quest).
-- ====================
target("npc_gfx_retry_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_npc_gfx_retry_test.c")
    add_files("src/engine/src/npcUtils.c")
target_end()


-- ====================
-- ROM-backed raw gfx-group EWRAM alias regression test.
-- ====================
target("gfx_group_dma_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_files("tools/tests/port/port_gfx_group_dma_test.c")
target_end()

-- ====================
-- 3DS diagnostic-dump Load State file selection/validation regression test.
-- ====================
target("dump_state_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH", "REVISION=0")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_files("src/port/port_dump_state.c")
    add_files("tools/tests/port/port_dump_state_test.c")
target_end()


-- ====================
-- Hidden-HUD second-screen charge-meter regression test.
-- ====================
target("second_screen_charge_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_packages("libsdl3")
    add_defines("PORT_SECOND_SCREEN_TEST")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_second_screen_charge_test.c")
    add_files("src/port/port_second_screen.c")
target_end()


-- ====================
-- Bottom-screen Kinstone OBJ-footprint regression test (NEW-4d).
-- ====================
target("second_screen_kinstone_layout_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH", "PORT_SECOND_SCREEN_QUEST_TEST")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_second_screen_kinstone_layout_test.c")
    add_files("src/port/port_second_screen_quest.c")
target_end()


-- ====================
-- Exact Kinstone report / E1 EU fuser-migration regression fixtures.
-- ====================
target("kinstone_report_fixtures_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("tools/tests/port/port_kinstone_report_fixtures_test.c")
target_end()


-- ====================
-- Enter-room banner widescreen lifecycle regression test.
-- ====================
target("widescreen_banner_state_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port")
    add_files("tools/tests/port/port_widescreen_banner_state_test.c")
target_end()


-- ====================
-- Pullable-mushroom transactional allocation regression test.
-- ====================
target("pullable_mushroom_allocation_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/pullable_mushroom_allocation_test.c")
    add_files("src/engine/src/object/pullableMushroom.c")
target_end()


-- ====================
-- Transition-time enemy-target null-safety regression test.
-- ====================
target("enemy_target_null_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_enemy_target_null_test.c")
    add_files("src/engine/src/enemyUpdate.c")
target_end()


-- ====================
-- Delayed scripted-entity transactional spawn regression test.
-- ====================
target("delayed_entity_script_slot_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_delayed_entity_script_slot_test.c")
    add_files("src/engine/src/manager/delayedEntityLoadManager.c")
target_end()


-- ====================
-- Semantic flag remap fail-closed runtime regression test.
-- ====================
target("flag_runtime_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("src/engine/src/flags.c")
    add_files("src/port/flag_remap_generated.c")
    add_files("tools/tests/port/port_flag_runtime_test.c")
target_end()


-- ====================
-- Compiled-USA Dialog local-flag provenance regression test.
-- ====================
target("dialog_region_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("src/engine/src/npcUtils.c")
    add_files("tools/tests/port/port_dialog_region_test.c")
target_end()


-- ====================
-- Compiled-USA -> region-native entity/tile data regression test.
-- ====================
target("region_data_resolver_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("tools/tests/port/port_region_data_test.c")
    add_files("src/port/port_region_data.c")
target_end()


-- ====================
-- Cloud Tops EU/JP fight flag parity regression test.
-- ====================
target("cloud_tops_fight_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_cloud_tops_fight_test.c")
    add_files("src/port/port_cloud_tops_fight.c")
target_end()


-- ====================
-- USA/EU Vaati progression invariant and one-shot repair regression test.
-- ====================
target("vaati_progress_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "USA", "ENGLISH")
    add_files("tools/tests/port/port_vaati_progress_test.c")
    add_files("src/port/port_vaati_progress.c")
target_end()


-- ====================
-- Inline text-variable formatting used by figurine names.
-- ====================
target("text_variables_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_files("tools/tests/port/port_text_variables_test.c")
target_end()


-- ====================
-- Spiked-roller signed-range trajectory regression test.
-- ====================
target("spiked_rollers_motion_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_spiked_rollers_motion_test.c")
    add_files("src/engine/src/projectile/spikedRollers.c")
target_end()


-- ====================
-- Gleerok affine-flame geometry and last-damage quick-dump diagnostics.
-- ====================
target("gleerok_fire_hitbox_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_gleerok_fire_hitbox_test.c")
    add_files("src/engine/src/object/gleerokParticle.c")
    add_files("src/engine/src/collision.c")
target_end()


-- ====================
-- Bow Moblin arrow direction table and exact retail collision boundaries.
-- ====================
target("arrow_projectile_hitbox_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_arrow_projectile_hitbox_test.c")
    add_files("src/engine/src/projectile/arrowProjectile.c")
    add_files("src/engine/src/playerHitbox.c")
    add_files("src/engine/src/collision.c")
target_end()


-- ====================
-- Canonical 0x500-byte EEPROM save-record layout regression test.
-- ====================
target("save_layout_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_files("tools/tests/port/port_save_layout_test.c")
target_end()


-- ====================
-- Fail-closed raw EEPROM persistence and interrupted-write recovery test.
-- ====================
target("save_persistence_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_files("src/port/port_save.c")
    add_files("tools/tests/port/port_save_persistence_test.c")
target_end()


-- ====================
-- End-to-end E1 raw-profile migration through production ReadSaveFile.
-- ====================
target("save_migration_integration_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("src/port/port_save.c")
    add_files("src/engine/src/save.c")
    add_files("tools/tests/port/port_save_migration_integration_test.c")
target_end()


-- ====================
-- End-to-end E1 EU Kinstone repair through production GetFusionToOffer.
-- ====================
target("kinstone_integration_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_includedirs("src/offsets/USA")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("src/port/port_save.c")
    add_files("src/engine/src/common.c")
    add_files("tools/tests/port/port_kinstone_integration_test.c")
target_end()


-- ====================
-- Story invariant, regional fusion-marker, and bounded fuser-data test.
-- ====================
target("save_story_region_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("src/port/flag_remap_generated.c")
    add_files("tools/tests/port/port_save_story_region_test.c")
target_end()


-- ====================
-- Retail tile-interaction table regression test.
-- ====================
target("tile_interaction_data_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_files("tools/tests/port/port_tile_interaction_data_test.c")
    add_files("src/engine/src/data/data_080046A4.c")
target_end()


-- ====================
-- Multi-region Castle Maid script-callback regression test.
-- ====================
target("castle_maid_dialog_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_files("tools/tests/port/port_castle_maid_dialog_test.c")
target_end()


-- ====================
-- 3DS ARM11 host-pointer regression test. This builds the TMC_3DS branch on
-- the desktop with a mocked memory-map query.
-- ====================
-- ====================
-- Nine-slice mapping regression test. DrawSliced carries sd/se/modulus
-- incrementally instead of calling SliceMap (four software divides) per pixel;
-- this proves the two agree for every pixel across the parameter space.
-- ====================
target("port_second_screen_slicemap_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_files("tools/tests/port/port_second_screen_slicemap_test.c")
target_end()


target("host_pointer_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT", "TMC_3DS")
    add_files("src/port/port_gba_mem.c")
    add_files("tools/tests/port/port_host_pointer_3ds_test.c")
target_end()


-- ====================
-- 3DS title/file-select bottom-screen procedural render regression test.
-- ====================
target("bottom_idle_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/bottom_idle_3ds.c")
    add_files("platform/3ds/tests/bottom_idle_3ds_test.c")
target_end()


-- ====================
-- 3DS bottom-screen paint/submit/visibility generation regression test.
-- ====================
target("bottom_frame_state_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/bottom_frame_state_3ds.c")
    add_files("platform/3ds/tests/bottom_frame_state_3ds_test.c")
target_end()

-- ====================
-- 3DS MAP-tab repaint-skip signature regression test.
-- ====================
target("bottom_map_anim_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/bottom_map_anim_3ds.c")
    add_files("platform/3ds/tests/bottom_map_anim_3ds_test.c")
    add_syslinks("m")
target_end()


-- ====================
-- MP2K track mixdown bit-exactness regression test.
-- ====================
target("port_m4a_mixdown_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port")
    add_files("src/port/port_m4a_mixdown.c")
    add_files("tools/tests/port/port_m4a_mixdown_test.c")
    add_syslinks("m")
target_end()
-- ====================
-- Old/New 3DS GPU upload layout regression test.
-- ====================
target("platform_gpu_layout_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source", "src/port")
    add_files("platform/3ds/tests/platform_gpu_layout_3ds_test.c")
target_end()


target("port_ppu_gpu_3ds_bench")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_defines("MODE1_GBA_WIDTH=266", "PPU_GPU3DS_PROFILE", "_POSIX_C_SOURCE=200809L")
    add_includedirs("platform/3ds/source", "src/port/ppu/include")
    add_files("platform/3ds/source/port_ppu_gpu_3ds_model.c")
    -- The software rasterizer is the oracle the GPU model must agree with;
    -- comparing the two map-space paths against each other cannot catch a
    -- fault they share.
    add_files("src/port/ppu/src/*.c")
    add_files("platform/3ds/tests/port_ppu_gpu_3ds_bench.c")
target_end()


target("ppu_gpu_3ds_budget_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source", "src/port/ppu/include")
    add_files("platform/3ds/tests/ppu_gpu_3ds_budget_test.c")
target_end()

target("port_ppu_gpu_3ds_model_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_defines("MODE1_GBA_WIDTH=266")
    add_includedirs("platform/3ds/source", "src/port/ppu/include")
    add_files("platform/3ds/source/port_ppu_gpu_3ds_model.c")
    add_files("platform/3ds/tests/port_ppu_gpu_3ds_model_test.c")
target_end()


-- ====================
-- Old 3DS 60 Hz logic-target / adaptive presentation pacing regression test.
-- ====================
target("old3ds_frame_pacer_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/old3ds_frame_pacer.c")
    add_files("platform/3ds/tests/old3ds_frame_pacer_test.c")
target_end()


-- ====================
-- Deterministic nearest-resampler PCM parity/CRC and microbenchmark. This
-- guards the allocation-free callback path used by the 3DS audio mixer.
-- ====================
target("audio_resampler_test")
    set_kind("binary")
    set_languages("cxx20")
    set_targetdir("build/pc")
    add_includedirs("src/third_party/agbplay_core")
    add_includedirs("src/port")
    add_files("src/third_party/agbplay_core/Resampler.cpp")
    add_files("src/third_party/agbplay_core/tests/audio_resampler_test.cpp")
    add_syslinks("m")
target_end()

target("audio_mix_bench")
    set_kind("binary")
    set_languages("cxx20")
    set_targetdir("build/pc")
    add_includedirs("src/third_party/agbplay_core")
    add_includedirs("src/port")
    add_defines("TMC_3DS")
    add_files("src/third_party/agbplay_core/*.cpp")
    add_files("tools/audio_mix_bench.cpp")
    add_syslinks("m")
target_end()


-- ====================
-- Memory-watch list regression test (fault-safe little-endian reads + watch
-- bookkeeping; see port_memory_watch_test.c)
-- ====================
target("memory_watch_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("src/port")
    add_includedirs("src/engine/include")
    add_defines("PC_PORT")
    add_files("src/port/port_debug_memory_watch.c")
    add_files("tools/tests/port/port_memory_watch_test.c")
target_end()


-- ====================
-- GPU-vs-CPU PPU parity harness (docs/gpu-rasterizer-design.md). Renders
-- synthetic GBA memory states through the CPU software rasterizer and the
-- GPU rasterizer and diffs them pixel-for-pixel. Headless SDL_GPU on the
-- `offscreen` video driver. The PPU .c files build as C (matching the game)
-- and link against the C++ GPU raster module.
-- ====================
target("mode1_native_fast_path_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port/ppu/include")
    add_defines("VIRTUAPPU_TESTING", "MODE1_GBA_WIDTH=266")
    add_files("src/port/ppu/tests/mode1_native_fast_path_test.c")
    add_files("src/port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()


target("mode1_draw_order_profile_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("src/port/ppu/include")
    add_defines("VIRTUAPPU_TESTING", "MODE1_GBA_WIDTH=266")
    add_files("src/port/ppu/tests/mode1_draw_order_profile_test.c")
    add_files("src/port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()


target("subtask_affine_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include", "src/port/SDL3")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_subtask_affine_test.c", "src/engine/src/subtask.c", "src/engine/src/code_0805EC04.c")
target_end()

-- Production palette reservation and enemy death allocation regressions.
for _, spec in ipairs({
    {"palette_release_test", "tools/tests/port/port_palette_release_test.c", "src/engine/src/color.c"},
    {"enemy_death_fx_test", "tools/tests/port/port_enemy_death_fx_test.c", "src/engine/src/enemyUtils.c"}
}) do
    target(spec[1])
        set_kind("binary")
        set_languages("c11")
        set_targetdir("build/pc")
        add_includedirs(".", "src/port", "src/engine/include")
        add_defines("PC_PORT", "USA", "ENGLISH")
        add_cflags("-ffunction-sections")
        if is_plat("macosx") then
            add_ldflags("-Wl,-dead_strip")
        else
            add_ldflags("-Wl,--gc-sections")
        end
        add_files(spec[2], spec[3])
    target_end()
end

target("gfx_slots_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_gfx_slots_test.c", "src/engine/src/vram.c")
target_end()

-- Native equivalents of the three retail EU Backport corrections.
target("eu_backport_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include", "src/port/SDL3")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_eu_backport_test.c", "src/engine/src/npc/farmers.c", "src/engine/src/roomInit.c",
              "src/engine/src/itemMetaData.c", "src/engine/src/itemUtils.c")
target_end()

target("second_screen_state_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include", "platform/3ds/source")
    add_defines("PC_PORT", "TMC_3DS", "USA", "ENGLISH")
    add_files("src/port/port_second_screen_state.c", "tools/tests/port/port_second_screen_state_test.c")
target_end()

-- Legacy bomb ownership recovery and the actual bag reward path.
target("bomb_compat_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include", "src/offsets/USA")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("src/port/port_bomb_compat.c", "tools/tests/port/port_bomb_compat_test.c", "src/engine/src/itemUtils.c", "src/engine/src/itemMetaData.c")
target_end()

-- Production tile streaming and reveal continuity across adjacent rooms.
target("widescreen_stream_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/engine/include", "src/port", "src/port/ppu/include")
    add_defines("PC_PORT", "USA", "ENGLISH", "MODE1_GBA_WIDTH=400", "MODE1_GBA_HEIGHT=240")
    add_packages("libsdl3")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("tools/tests/port/port_widescreen_stream_test.c", "src/port/port_linked_stubs.c", "src/port/port_widescreen.c")
    add_files("src/port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()

-- Exercise the production 3DS wrapper without SDK/hardware dependencies.
target("bottom_map_camera_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".", "src/port", "src/engine/include", "platform/3ds/source")
    add_defines("PC_PORT", "TMC_3DS", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
        add_syslinks("m")
    end
    add_files("platform/3ds/tests/bottom_map_camera_3ds_test.c",
              "platform/3ds/source/bottom_map_anim_3ds.c")
target_end()

-- Production top-screen aspect/filter policy, independent of device SDKs.
target("top_view_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/tests/top_view_3ds_test.c", "platform/3ds/source/top_view_3ds.c")
target_end()
