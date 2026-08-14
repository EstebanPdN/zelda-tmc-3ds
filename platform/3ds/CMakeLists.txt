cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" TMC_3DS_VERSION LIMIT_COUNT 1)
project(tmc_3ds VERSION "${TMC_3DS_VERSION}" LANGUAGES C CXX)

set(TMC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

file(GLOB TMC_ACTOR_SOURCES CONFIGURE_DEPENDS
    "${TMC_ROOT}/src/playerItem/*.c"
    "${TMC_ROOT}/src/enemy/*.c"
    "${TMC_ROOT}/src/npc/*.c"
    "${TMC_ROOT}/src/object/*.c"
    "${TMC_ROOT}/src/projectile/*.c"
    "${TMC_ROOT}/src/manager/*.c"
    "${TMC_ROOT}/src/item/*.c"
    "${TMC_ROOT}/src/subtask/*.c"
    "${TMC_ROOT}/src/menu/*.c"
    "${TMC_ROOT}/src/worldEvent/*.c"
)

file(GLOB AGBPLAY_SOURCES CONFIGURE_DEPENDS "${TMC_ROOT}/libs/agbplay_core/*.cpp")

set(TMC_ENGINE_SOURCES
    src/main.c src/common.c src/interrupts.c src/game.c src/title.c src/fileselect.c
    src/flags.c src/save.c src/fade.c src/color.c src/vram.c src/screenTileMap.c
    src/message.c src/text.c src/sound.c
    src/script.c src/scroll.c src/room.c src/roomInit.c src/entity.c src/physics.c
    src/collision.c src/movement.c src/affine.c src/sineTable.c src/ui.c
    src/player.c src/playerUtils.c src/playerHitbox.c src/playerItem.c
    src/playerItemUtils.c src/playerItemDefinitions.c src/enemy.c src/enemyUtils.c
    src/npc.c src/npcUtils.c src/npcFunctions.c src/npcDefinitions.c src/object.c
    src/objectUtils.c src/objectDefinitions.c src/enemyUpdate.c src/projectile.c
    src/projectileUtils.c src/projectileUpdate.c src/manager.c src/item.c
    src/itemUtils.c src/itemDefinitions.c src/itemMetaData.c src/subtask.c
    src/cutscene.c src/backgroundAnimations.c src/beanstalkSubtask.c
    src/enterPortalSubtask.c src/gameOverTask.c src/gameUtils.c src/gameData.c
    src/kinstone.c src/droptables.c src/demo.c src/debug.c src/flagDebug.c
    src/staffroll.c src/data/figurineMenuData.c src/data/hitbox.c
    src/data/areaMetadata.c src/data/caveBorderMapData.c src/data/data_080046A4.c
    src/data/mapActTileToSurfaceType.c src/data/objPalettes.c
    src/data/screenTransitions.c src/data/transitions.c src/code_08049CD4.c
    src/code_08049DF4.c src/code_0805EC04.c
)
list(TRANSFORM TMC_ENGINE_SOURCES PREPEND "${TMC_ROOT}/")

set(TMC_PORT_SOURCES
    port/port_bios.c port/port_debug_verbose.c port/port_inline_ptrs.c
    port/port_script_addrs.c port/port_offset_remap.c port/port_rom.c
    port/port_stubs.c port/stubs_autogen.c port/data_stubs_autogen.c
    port/data_const_stubs.c port/flag_remap_generated.c port/port_rom_tables.c
    port/port_linked_stubs.c port/port_figurines.c port/port_draw.c
    port/port_gba_mem.c port/port_hdma.c port/port_save.c port/port_softslots.c
    port/port_second_screen_state.c
    port/port_second_screen_render.c port/port_second_screen_worldmap.c
    port/port_second_screen_dungeonmap.c port/port_second_screen_quest.c
    port/port_second_screen_theme.c port/port_roll_attack_macro.c
    port/port_filter.c port/port_animation.c port/port_math.c
    port/port_text_render.c port/port_gameplay_stubs.c port/port_m4a_stubs.c
    port/port_room_funcs.c port/port_script_funcs.c port/port_analog_movement.c
    port/port_m4a_backend.cpp port/generated_sounds_embed.cpp
    port/ppu/src/virtuappu.c port/ppu/src/mode1.c
    port/rando/rando.cpp port/rando/rando_logic.cpp
    port/rando/rando_save.c port/rando/rando_runtime.c
    port/rando/rando_newfile.c port/rando/rando_entrance.cpp
    port/rando/rando_cosmetic.cpp port/rando/rando_music.c
    port/rando/rando_keymap.c
)
list(TRANSFORM TMC_PORT_SOURCES PREPEND "${TMC_ROOT}/")

add_executable(tmc-3ds
    ${TMC_ENGINE_SOURCES}
    ${TMC_ACTOR_SOURCES}
    ${TMC_PORT_SOURCES}
    ${AGBPLAY_SOURCES}
    source/main_3ds.c
    source/platform_3ds.c
    source/platform_gpu_3ds.c
    source/bottom_idle_3ds.c
    source/port_second_screen_3ds.c
    source/port_second_screen_sync_3ds.c
    source/port_ppu_3ds.c
    source/port_audio_3ds.c
    source/port_config_3ds.c
    source/port_rando_3ds.c
    source/port_russian_3ds.c
    source/port_asset_stubs_3ds.c
    source/port_optional_stubs_3ds.c
)

target_include_directories(tmc-3ds PRIVATE
    "${TMC_ROOT}" "${TMC_ROOT}/include" "${TMC_ROOT}/port"
    "${TMC_ROOT}/port/ppu/include" "${TMC_ROOT}/libs/agbplay_core"
    "${TMC_ROOT}/build/USA" "${CMAKE_CURRENT_LIST_DIR}/source"
)

target_compile_definitions(tmc-3ds PRIVATE
    PC_PORT NON_MATCHING USE_HDMA TMC_3DS MULTI_REGION USA ENGLISH REVISION=0
    MODE1_GBA_WIDTH=266 TMC_PORT_VERSION="${TMC_3DS_VERSION}" TMC_PC_VERSION="${TMC_3DS_VERSION}"
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
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition"
)
set_source_files_properties(
    "${TMC_ROOT}/port/port_m4a_backend.cpp"
    ${AGBPLAY_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition;-ffast-math"
)
target_link_options(tmc-3ds PRIVATE -O3 -flto -Wl,--gc-sections)
target_link_libraries(tmc-3ds PRIVATE citro2d citro3d ctru m)

set(TMC_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${TMC_ROMFS}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/splash.rgb565"
    "${TMC_ROMFS}/splash.rgb565"
    COPYONLY
)
file(MAKE_DIRECTORY "${TMC_ROMFS}/ru")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/ru/russian.bin"
    "${TMC_ROMFS}/ru/russian.bin"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/ru/russian_font.bin"
    "${TMC_ROMFS}/ru/russian_font.bin"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/ru/russian_banner_font.bin"
    "${TMC_ROMFS}/ru/russian_banner_font.bin"
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
