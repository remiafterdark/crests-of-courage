cmake_minimum_required(VERSION 3.25)
project(coop_mod CXX)

set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dusklight" CACHE PATH "Path to dusk source root")
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
             src/projectiles.cpp
             src/effects.cpp
             src/pvp.cpp
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
    MOD_JSON mod.json
)

option(COOP_PUBLIC_BUILD "Build for publishing" OFF)
if(COOP_PUBLIC_BUILD AND MSVC)
    target_link_options(coop_mod PRIVATE "/PDBALTPATH:%_PDB%")
endif()
