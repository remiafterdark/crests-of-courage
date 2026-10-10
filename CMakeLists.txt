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

include(FetchContent)
set(COOP_VOICE ON)
set(COOP_VOICE_SDL OFF)
if (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(COOP_VOICE_SDL ON)
endif ()
if (COOP_VOICE)
    block()
        set(BUILD_SHARED_LIBS OFF)
        set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(coop_opus
            GIT_REPOSITORY https://github.com/xiph/opus.git
            GIT_TAG v1.5.2
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(coop_opus)
        set_target_properties(opus PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endblock()
endif ()
if (COOP_VOICE_SDL)
    file(GLOB COOP_SDL3_HINTS "${DUSK_DIR}/build/*/_deps/sdl3_prebuilt-src/include"
                              "${DUSK_DIR}/build/*/_deps/sdl3-src/include")
    find_path(COOP_SDL3_INCLUDE_DIR SDL3/SDL_audio.h HINTS ${COOP_SDL3_HINTS} NO_DEFAULT_PATH)
    if (NOT COOP_SDL3_INCLUDE_DIR)
        FetchContent_Declare(coop_sdl3_headers
            URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.10.tar.gz"
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
        FetchContent_GetProperties(coop_sdl3_headers)
        if (NOT coop_sdl3_headers_POPULATED)
            FetchContent_Populate(coop_sdl3_headers)
        endif ()
        set(COOP_SDL3_INCLUDE_DIR "${coop_sdl3_headers_SOURCE_DIR}/include")
    endif ()
endif ()

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
             src/softlocks.cpp
             src/storytest.cpp
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
             src/crash_guard.cpp
             src/global_hud.cpp
             src/chat_keys.cpp
             src/voice.cpp
             src/voice_io.cpp
    MOD_JSON mod.json
)

if (COOP_VOICE)
    target_link_libraries(coop_mod PRIVATE opus)
    target_compile_definitions(coop_mod PRIVATE COOP_VOICE=1)
endif ()
if (COOP_VOICE_SDL)
    target_include_directories(coop_mod PRIVATE "${COOP_SDL3_INCLUDE_DIR}")
    target_compile_definitions(coop_mod PRIVATE COOP_VOICE_SDL=1)
elseif (APPLE)
    target_link_libraries(coop_mod PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
elseif (ANDROID)
    target_link_libraries(coop_mod PRIVATE aaudio)
endif ()

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
