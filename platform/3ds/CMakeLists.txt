cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" TMC_3DS_VERSION LIMIT_COUNT 1)
# CMake's project VERSION accepts numeric components only, while experimental
# packages use a visible suffix such as 1.1-E1. Keep the complete release
# string for filenames and metadata, and give project() its numeric prefix.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" TMC_3DS_PROJECT_VERSION "${TMC_3DS_VERSION}")
if(NOT TMC_3DS_PROJECT_VERSION)
    message(FATAL_ERROR "Invalid 3DS version: ${TMC_3DS_VERSION}")
endif()
project(tmc_3ds VERSION "${TMC_3DS_PROJECT_VERSION}" LANGUAGES C CXX)

set(TMC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

file(GLOB TMC_ACTOR_SOURCES CONFIGURE_DEPENDS
    "${TMC_ROOT}/engine/src/playerItem/*.c"
    "${TMC_ROOT}/engine/src/enemy/*.c"
    "${TMC_ROOT}/engine/src/npc/*.c"
    "${TMC_ROOT}/engine/src/object/*.c"
    "${TMC_ROOT}/engine/src/projectile/*.c"
    "${TMC_ROOT}/engine/src/manager/*.c"
    "${TMC_ROOT}/engine/src/item/*.c"
    "${TMC_ROOT}/engine/src/subtask/*.c"
    "${TMC_ROOT}/engine/src/menu/*.c"
    "${TMC_ROOT}/engine/src/worldEvent/*.c"
)

file(GLOB AGBPLAY_SOURCES CONFIGURE_DEPENDS "${TMC_ROOT}/libs/agbplay_core/*.cpp")

set(TMC_ENGINE_SOURCES
    engine/src/main.c engine/src/common.c engine/src/interrupts.c engine/src/game.c engine/src/title.c engine/src/fileselect.c
    engine/src/flags.c engine/src/save.c engine/src/fade.c engine/src/color.c engine/src/vram.c engine/src/screenTileMap.c
    engine/src/message.c engine/src/text.c engine/src/sound.c
    engine/src/script.c engine/src/scroll.c engine/src/room.c engine/src/roomInit.c engine/src/entity.c engine/src/physics.c
    engine/src/collision.c engine/src/movement.c engine/src/affine.c engine/src/sineTable.c engine/src/ui.c
    engine/src/player.c engine/src/playerUtils.c engine/src/playerHitbox.c engine/src/playerItem.c
    engine/src/playerItemUtils.c engine/src/playerItemDefinitions.c engine/src/enemy.c engine/src/enemyUtils.c
    engine/src/npc.c engine/src/npcUtils.c engine/src/npcFunctions.c engine/src/npcDefinitions.c engine/src/object.c
    engine/src/objectUtils.c engine/src/objectDefinitions.c engine/src/enemyUpdate.c engine/src/projectile.c
    engine/src/projectileUtils.c engine/src/projectileUpdate.c engine/src/manager.c engine/src/item.c
    engine/src/itemUtils.c engine/src/itemDefinitions.c engine/src/itemMetaData.c engine/src/subtask.c
    engine/src/cutscene.c engine/src/backgroundAnimations.c engine/src/beanstalkSubtask.c
    engine/src/enterPortalSubtask.c engine/src/gameOverTask.c engine/src/gameUtils.c engine/src/gameData.c
    engine/src/kinstone.c engine/src/droptables.c engine/src/demo.c engine/src/debug.c engine/src/flagDebug.c
    engine/src/staffroll.c engine/src/data/figurineMenuData.c engine/src/data/hitbox.c
    engine/src/data/areaMetadata.c engine/src/data/caveBorderMapData.c engine/src/data/data_080046A4.c
    engine/src/data/mapActTileToSurfaceType.c engine/src/data/objPalettes.c
    engine/src/data/screenTransitions.c engine/src/data/transitions.c engine/src/code_08049CD4.c
    engine/src/code_08049DF4.c engine/src/code_0805EC04.c
)
list(TRANSFORM TMC_ENGINE_SOURCES PREPEND "${TMC_ROOT}/")

set(TMC_PORT_SOURCES
    port/port_bios.c port/port_debug_verbose.c port/port_inline_ptrs.c
    port/port_script_addrs.c port/port_offset_remap.c port/port_rom.c
    port/port_region_data.c
    port/port_bottle_compat.c
    port/port_bomb_compat.c
    port/port_cloud_tops_fight.c
    port/port_vaati_progress.c
    port/port_stubs.c port/stubs_autogen.c port/data_stubs_autogen.c
    port/data_const_stubs.c port/flag_remap_generated.c port/port_rom_tables.c
    port/port_linked_stubs.c port/port_widescreen.c port/port_figurines.c port/port_draw.c
    port/port_gba_mem.c port/port_hdma.c port/port_save.c port/port_softslots.c
    port/port_dump_state.c
    port/port_second_screen_state.c
    port/port_second_screen_render.c port/port_second_screen_worldmap.c
    port/port_second_screen_dungeonmap.c port/port_second_screen_quest.c
    port/port_second_screen_theme.c port/port_roll_attack_macro.c
    port/port_filter.c port/port_animation.c port/port_math.c
    port/port_text_render.c port/port_gameplay_stubs.c port/port_m4a_stubs.c
    port/port_room_funcs.c port/port_script_funcs.c port/port_analog_movement.c
    port/port_m4a_backend.cpp port/port_m4a_mixdown.c port/generated_sounds_embed.cpp
    port/ppu/src/virtuappu.c port/ppu/src/mode1.c
)
list(TRANSFORM TMC_PORT_SOURCES PREPEND "${TMC_ROOT}/")

ctr_add_shader_library(ppu_gpu_3ds_shader source/ppu_gpu_3ds.v.pica)
dkp_add_embedded_binary_library(ppu_gpu_3ds_shader_bin ppu_gpu_3ds_shader)

add_executable(tmc-3ds
    ${TMC_ENGINE_SOURCES}
    ${TMC_ACTOR_SOURCES}
    ${TMC_PORT_SOURCES}
    ${AGBPLAY_SOURCES}
    source/updater.c
    source/update_manifest.c
    source/main_3ds.c
    source/arm11_fast_mem.c
    source/ndsp_pcm_offload.c
    source/ndsp_psg_offload.c
    source/platform_3ds.c
    source/old3ds_frame_pacer.c
    source/top_view_3ds.c
    source/platform_gpu_3ds.c
    source/bottom_idle_3ds.c
    source/bottom_frame_state_3ds.c
    source/bottom_map_anim_3ds.c
    source/speaker_eq_3ds.c
    source/port_second_screen_3ds.c
    source/port_second_screen_sync_3ds.c
    source/port_ppu_3ds.c
    source/port_ppu_gpu_3ds_model.c
    source/port_ppu_gpu_3ds.c
    source/port_audio_3ds.c
    source/port_config_3ds.c
    source/port_dump_state_3ds.c
    source/port_asset_stubs_3ds.c
    source/port_optional_stubs_3ds.c
)

target_include_directories(tmc-3ds PRIVATE
    "${TMC_ROOT}" "${TMC_ROOT}/engine/include" "${TMC_ROOT}/port"
    "${TMC_ROOT}/port/ppu/include" "${TMC_ROOT}/libs/agbplay_core"
    "${TMC_ROOT}/build/USA" "${CMAKE_CURRENT_LIST_DIR}/source"
)

target_compile_definitions(tmc-3ds PRIVATE
    PC_PORT NON_MATCHING USE_HDMA TMC_3DS MULTI_REGION USA ENGLISH REVISION=0
    MODE1_GBA_WIDTH=400 MODE1_GBA_HEIGHT=240 PPU_GPU3DS_PROFILE
    TMC_PORT_VERSION="${TMC_3DS_VERSION}" TMC_PC_VERSION="${TMC_3DS_VERSION}"
)
target_compile_features(tmc-3ds PRIVATE c_std_11 cxx_std_20)
set_target_properties(tmc-3ds PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)
target_compile_options(tmc-3ds PRIVATE
    -include region.h
    -O3 -fomit-frame-pointer -ffunction-sections -fdata-sections
    -fno-strict-aliasing -fwrapv -fno-strict-overflow -w
    -Wno-unused-parameter -Wno-missing-field-initializers
)
set_source_files_properties(
    "${TMC_ROOT}/port/ppu/src/virtuappu.c"
    "${TMC_ROOT}/port/ppu/src/mode1.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_second_screen_3ds.c"
    "${TMC_ROOT}/port/port_second_screen_render.c"
    "${TMC_ROOT}/port/port_second_screen_worldmap.c"
    "${TMC_ROOT}/port/port_second_screen_dungeonmap.c"
    "${TMC_ROOT}/port/port_second_screen_quest.c"
    "${TMC_ROOT}/port/port_second_screen_theme.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/platform_gpu_3ds.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_3ds.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_gpu_3ds_model.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_gpu_3ds.c"
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition"
)
set_source_files_properties(
    "${TMC_ROOT}/port/port_m4a_backend.cpp"
    # The mixdown was carved out of port_m4a_backend.cpp, so it must keep that
    # file's flags. Compiled without -ffast-math it would differ from the code
    # it replaced in both rounding and speed.
    "${TMC_ROOT}/port/port_m4a_mixdown.c"
    ${AGBPLAY_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition;-ffast-math"
)
target_link_options(tmc-3ds PRIVATE -O3 -flto -Wl,--gc-sections)
set(UPDATE_DEPS_ROOT "$ENV{DEVKITPRO}/portlibs/3ds" CACHE PATH "3DS updater dependencies")
target_include_directories(tmc-3ds PRIVATE "${UPDATE_DEPS_ROOT}/include")
target_link_directories(tmc-3ds PRIVATE "${UPDATE_DEPS_ROOT}/lib")
# Official devkitPro curl enables gzip/deflate; the pinned minimal build does
# not. Link its optional static dependency when supplied by the selected SDK.
find_library(TMC_UPDATE_ZLIB NAMES z PATHS "${UPDATE_DEPS_ROOT}/lib"
    "$ENV{DEVKITPRO}/portlibs/3ds/lib" NO_DEFAULT_PATH)
if(TMC_UPDATE_ZLIB)
    target_link_libraries(tmc-3ds PRIVATE citro2d citro3d curl jansson mbedtls mbedx509 mbedcrypto "${TMC_UPDATE_ZLIB}" ctru m ppu_gpu_3ds_shader_bin)
else()
    target_link_libraries(tmc-3ds PRIVATE citro2d citro3d curl jansson mbedtls mbedx509 mbedcrypto ctru m ppu_gpu_3ds_shader_bin)
endif()

set(TMC_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${TMC_ROMFS}")
# Include redistribution notices with each installable package.
file(MAKE_DIRECTORY "${TMC_ROMFS}/licenses")
configure_file("${TMC_ROOT}/LICENSE" "${TMC_ROMFS}/licenses/GPL-3.0.txt" COPYONLY)
configure_file("${TMC_ROOT}/COPYRIGHT.md" "${TMC_ROMFS}/licenses/COPYRIGHT.md" COPYONLY)
configure_file("${TMC_ROOT}/THIRD-PARTY-LICENSES.md" "${TMC_ROMFS}/licenses/THIRD-PARTY-LICENSES.md" COPYONLY)
configure_file("${TMC_ROOT}/libs/agbplay_core/LICENSE" "${TMC_ROMFS}/licenses/agbplay-NOTICE.txt" COPYONLY)
configure_file("${TMC_ROOT}/libs/agbplay_core/COPYING.LESSER" "${TMC_ROMFS}/licenses/LGPL-3.0.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/update-ca.pem" "${TMC_ROMFS}/update-ca.pem" COPYONLY)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/splash.rgb565"
    "${TMC_ROMFS}/splash.rgb565"
    COPYONLY
)

set(TMC_SMDH "${CMAKE_CURRENT_BINARY_DIR}/tmc-3ds.smdh")
ctr_generate_smdh(
    OUTPUT "${TMC_SMDH}"
    NAME "The Minish Cap 3DS v${TMC_3DS_VERSION}"
    DESCRIPTION "Native Nintendo 3DS port"
    AUTHOR "Esteban PDN / Project Picori / samyost1"
    ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon-48.png"
)

ctr_create_3dsx(tmc-3ds-3dsx
    TARGET tmc-3ds
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tmc-3ds-v${TMC_3DS_VERSION}.3dsx"
    SMDH "${TMC_SMDH}"
    ROMFS "${TMC_ROMFS}"
)
