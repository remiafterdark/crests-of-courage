cmake_minimum_required(VERSION 3.25)
project(coop_mod CXX)

set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dusklight" CACHE PATH "Path to dusk source root")
if (NOT EXISTS "${DUSK_DIR}/sdk/CMakeLists.txt")
    if (NOT DUSKLIGHT_VERSION)
        set(DUSKLIGHT_VERSION "b245c6bef8b4a370afb2453104a585dd41c97676")
    endif ()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
    set(DUSK_DIR "${DUSKLIGHT_DIR}")
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)

add_mod(coop_mod
    FEATURES game fmt
    SOURCES  src/mod.cpp
             src/puppet.cpp
             src/util.cpp
             src/models.cpp
             src/features.cpp
             src/menu.cpp
             src/colors.cpp
             src/upnp.cpp
             src/online.cpp
             src/skipvote.cpp
             src/drops.cpp
             src/twilight.cpp
             src/skills.cpp
             src/rando.cpp
             src/checks.cpp
             src/check_names.cpp
             src/ganon.cpp
             src/beast.cpp
             src/sumo.cpp
             src/map_markers.cpp
             src/notify.cpp
             src/selftest.cpp
             src/private_arc.cpp
             src/projectiles.cpp
             src/effects.cpp
             src/pvp.cpp
             src/spinner.cpp
             src/rival.cpp
             src/world.cpp
             src/enemies.cpp
             src/enemy_layout_wb.cpp
             src/enemy_layout_yk.cpp
             src/enemy_layout_po.cpp
             src/spawns.cpp
             src/boss.cpp
             src/join_sync.cpp
             src/epona.cpp
             src/grass.cpp
             src/party_hud.cpp
             src/skins.cpp
             src/icons.cpp
             src/voices.cpp
             src/game_mode.cpp
             src/horses.cpp
             src/report.cpp
             src/lan.cpp
             src/global.cpp
             src/chat.cpp
             src/chat_keys.cpp
    MOD_JSON mod.json
)

if (WIN32)
    target_link_libraries(coop_mod PRIVATE ws2_32 user32)
endif ()

if (APPLE)
    add_custom_command(TARGET coop_mod POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:coop_mod>"
        VERBATIM)
endif ()

if(MSVC)
    target_compile_options(coop_mod PRIVATE /Zc:__cplusplus)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" COOP_MOD_JSON)
string(JSON COOP_MOD_VERSION GET "${COOP_MOD_JSON}" version)
target_compile_definitions(coop_mod PRIVATE COOP_MOD_VERSION="${COOP_MOD_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mod.json")

option(COOP_PUBLIC_BUILD "Build for publishing" OFF)
if(COOP_PUBLIC_BUILD AND MSVC)
    target_link_options(coop_mod PRIVATE "/PDBALTPATH:%_PDB%")
endif()
