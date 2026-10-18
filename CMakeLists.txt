cmake_minimum_required(VERSION 3.16)
# CoDUO+ - the CoD:UO 1.51 port of COD1.6X (cod1plus/client). Builds mss32.dll,
# a proxy the engine loads from its own import table; see docs/ARCHITECTURE.md.
#
# C is here for src/shared/, which is plain C so that cod1plus.so (gcc, no C++) can
# compile the very same file. See src/shared/lean_controllers.h.
project(coduoplus LANGUAGES C CXX)

# CoD:UO is a 32-bit game. Force 32-bit build.
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "cod1reloaded must be built as 32-bit. "
        "Use a 32-bit MinGW (e.g. i686-w64-mingw32-g++) or pass -m32.")
endif()

# Native multilib gcc (msys2) needs -m32 to force 32-bit. The i686-w64-mingw32
# cross-compiler is already 32-bit and rejects -m32, so only add it natively.
if (NOT CMAKE_CROSSCOMPILING)
    add_compile_options(-m32)
    add_link_options(-m32)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COD1RELOADED_SOURCES
    # Compiled into cod1plus.so as well, from a byte-identical copy in that repo.
    # If you change it here, copy it there (and vice versa) - both sides print
    # lc_banner() at install so a drift shows up in the two logs.
    src/shared/lean_controllers.c
    src/core/main.cpp
    src/core/game.cpp
    src/core/patches.cpp
    src/gameplay/viewheight_fix.cpp
    src/gameplay/lean_fix.cpp
    src/gameplay/swing_fix.cpp
    src/core/logger.cpp
    src/core/iat.cpp
    src/input/rinput.cpp
    src/netcode/version_patch.cpp
    src/netcode/protocol_patch.cpp
    src/netcode/competitive.cpp
    src/features/pam_install.cpp
    src/netcode/version_gate.cpp
    src/video/window_patch.cpp
    src/video/fullscreen_patch.cpp
    src/video/display_probe.cpp
    src/video/mode_guard.cpp
    src/performance/fps_cap.cpp
    src/performance/frame_limiter.cpp
    src/performance/gpu_sync.cpp
    src/core/session_report.cpp
    src/core/ini_lock.cpp
    src/features/updater.cpp
    src/features/demo_upload.cpp
    src/core/toast.cpp
    src/performance/cpu_affinity.cpp
    src/performance/process_priority.cpp
    src/performance/working_set.cpp
    src/performance/fso_disable.cpp
    src/video/widescreen_fix.cpp
    src/video/gamma_fix.cpp
    src/features/avatar_overlay.cpp
    src/features/engine_2d.cpp
    src/features/discord_rpc.cpp
    src/features/settings_menu.cpp
    src/gameplay/stance_fix.cpp
    src/gameplay/anim_clamp.cpp
    src/netcode/antilag.cpp
    src/core/single_instance.cpp
    src/ui/gl_overlay.cpp
    src/ui/modern_menu.cpp
    src/ui/home_menu.cpp
    src/ui/player_preview.cpp
)

add_library(cod1reloaded SHARED
    ${COD1RELOADED_SOURCES}
)

add_library(mss32 SHARED
    ${COD1RELOADED_SOURCES}
    src/mss32_proxy.def
)

target_include_directories(cod1reloaded PRIVATE src)
target_include_directories(mss32 PRIVATE src)

# Build a DLL that never touches the mouse at all, for bisecting input problems.
# `raw_mouse_input = off` in the ini is NOT the same thing: it only sets the default of
# the m_rinput cvar, while rinput_start() still installs the GetCursorPos IAT hook at
# DllMain. With this option the hook is never installed and the engine's mouse path is
# exactly vanilla.
#     cmake -S . -B build-norinput -DCOD1RELOADED_NO_RINPUT=ON ...
option(COD1RELOADED_NO_RINPUT "Do not install the raw-input GetCursorPos hook" OFF)

# The two variants MUST NOT share an output directory. Both build trees resolve their
# output against CMAKE_SOURCE_DIR, so before this they both wrote <source>/build/mss32.dll
# and whichever was built last silently replaced the other - a release could ship the
# wrong one with nothing to show for it. The DLL cannot be renamed (it is a proxy and has
# to be called mss32.dll), so the variants are separated by DIRECTORY instead.
set(COD1RELOADED_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build")
if (COD1RELOADED_NO_RINPUT)
    target_compile_definitions(cod1reloaded PRIVATE COD1RELOADED_NO_RINPUT=1)
    target_compile_definitions(mss32 PRIVATE COD1RELOADED_NO_RINPUT=1)
    set(COD1RELOADED_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/norinput")
    message(STATUS "cod1reloaded: raw input DISABLED at compile time (no GetCursorPos hook)")
    message(STATUS "cod1reloaded: output -> ${COD1RELOADED_OUTPUT_DIR}")
endif()

# Dev builds only: core/ini_lock.cpp imposes nothing - the seven locked keys are taken as
# written in coduoplus.ini (e.g. gpu_sync = on to force the sync instead of auto) and
# r_fullscreen / r_displayRefresh are left to the engine. The log says so at every launch.
# Never ship this: the whole point of the lock is that everybody plays the same.
#     cmake -S . -B build-devunlock -DCOD1RELOADED_DEV_UNLOCK=ON ...   (upstream 1.6.10)
option(COD1RELOADED_DEV_UNLOCK "ini_lock imposes nothing: the .ini is taken as written (dev only)" OFF)
if (COD1RELOADED_DEV_UNLOCK)
    target_compile_definitions(cod1reloaded PRIVATE COD1RELOADED_DEV_UNLOCK=1)
    target_compile_definitions(mss32 PRIVATE COD1RELOADED_DEV_UNLOCK=1)
    set(COD1RELOADED_OUTPUT_DIR "${COD1RELOADED_OUTPUT_DIR}/devunlock")
    message(STATUS "coduoplus: DEV UNLOCK - ini_lock imposes nothing (do not ship)")
    message(STATUS "coduoplus: output -> ${COD1RELOADED_OUTPUT_DIR}")
endif()

target_compile_options(cod1reloaded PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -O2
)
target_compile_options(mss32 PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -O2
)

target_link_options(cod1reloaded PRIVATE
    -static -static-libgcc -static-libstdc++
    # -static embeds libwinpthread so the DLL has zero non-Windows deps.
    # No --kill-at: we want plain symbol names for now.
)
target_link_options(mss32 PRIVATE
    -static -static-libgcc -static-libstdc++
)

target_link_libraries(cod1reloaded PRIVATE
    kernel32 user32 winmm wininet shell32 advapi32 gdiplus gdi32 opengl32
)
target_link_libraries(mss32 PRIVATE
    kernel32 user32 winmm wininet shell32 advapi32 gdiplus gdi32 opengl32
)

set_target_properties(cod1reloaded PROPERTIES
    PREFIX ""
    OUTPUT_NAME "coduoplus"
    RUNTIME_OUTPUT_DIRECTORY "${COD1RELOADED_OUTPUT_DIR}"
)

set_target_properties(mss32 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mss32"
    RUNTIME_OUTPUT_DIRECTORY "${COD1RELOADED_OUTPUT_DIR}"
)

# -----------------------------------------------------------------------------
# Tests (ctest). site_check maps the stock CoD:UO 1.51 binaries from disk and
# checks, for every address the port patches, that the bytes the patch expects
# are really there - see tests/site_check.cpp and docs/TESTING.md.
#   cmake -B build -DCODUO_GAME_DIR="D:/Call of Duty - Copy"
#   cmake --build build && ctest --test-dir build --output-on-failure
# -----------------------------------------------------------------------------
set(CODUO_GAME_DIR "D:/Call of Duty - Copy" CACHE PATH
    "CoD:UO 1.51 install the tests read CoDUOMP.exe and uo/pakuo07.pk3 from")
find_package(Python3 COMPONENTS Interpreter)
enable_testing()

add_executable(site_check tests/site_check.cpp tests/image.cpp src/core/game.cpp)
target_include_directories(site_check PRIVATE src tests)
target_compile_options(site_check PRIVATE -Wall -Wextra -Wno-unused-parameter -O1)
target_link_options(site_check PRIVATE -static -static-libgcc -static-libstdc++)

if (Python3_FOUND)
    set(CODUO_TESTBIN "${CMAKE_BINARY_DIR}/testbin")
    add_test(NAME fetch_binaries
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tests/fetch_binaries.py"
                     "${CODUO_GAME_DIR}" "${CODUO_TESTBIN}")
    set_tests_properties(fetch_binaries PROPERTIES FIXTURES_SETUP uo_binaries)
    add_test(NAME site_check COMMAND site_check "${CODUO_TESTBIN}" "${CMAKE_SOURCE_DIR}/src/mss32_proxy.def")
    set_tests_properties(site_check PROPERTIES FIXTURES_REQUIRED uo_binaries)
    # the same checks against the 2005-01-10 uo_game_mp_x86.dll, when the install has it
    add_test(NAME site_check_2005
             COMMAND site_check "${CODUO_TESTBIN}-2005" "${CMAKE_SOURCE_DIR}/src/mss32_proxy.def" --optional)
    set_tests_properties(site_check_2005 PROPERTIES FIXTURES_REQUIRED uo_binaries SKIP_RETURN_CODE 77)
else()
    message(WARNING "Python 3 not found: site_check test not registered")
endif()

# Opt-in end-to-end test: launches the real game (as a dedicated server, console
# window only) from CODUO_GAME_DIR with the deployed mss32.dll. Off by default
# because it starts a process; enable with -DCODUO_SMOKE=ON and run deploy.ps1
# first. See tests/smoke_dedicated.py.
option(CODUO_SMOKE "Register the dedicated-server smoke test (launches CoDUOMP.exe)" OFF)
if (CODUO_SMOKE AND Python3_FOUND)
    add_test(NAME smoke_dedicated
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tests/smoke_dedicated.py" "${CODUO_GAME_DIR}")
    set_tests_properties(smoke_dedicated PROPERTIES TIMEOUT 120)
endif()

# The UI pk3 is generated from the install's own pakuo03.pk3 (ui_pk3/build_pk3.py);
# building it is the test that the stock menu is there and our item still fits.
if (Python3_FOUND)
    add_test(NAME ui_pk3
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/ui_pk3/build_pk3.py" --game "${CODUO_GAME_DIR}")
endif()

# Every relative link / #anchor in README, BUILD, CHANGELOG and docs/ resolves.
if (Python3_FOUND)
    add_test(NAME doc_links
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tests/check_doc_links.py"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# The real frame limiter wait inside a copy of UO's patched Com_Frame loop, against a
# fake engine (timing-based, ~15 s). See tests/test_frame_limiter.cpp.
add_executable(test_frame_limiter tests/test_frame_limiter.cpp
               src/performance/frame_limiter.cpp src/core/logger.cpp src/core/game.cpp)
target_include_directories(test_frame_limiter PRIVATE src)
target_compile_options(test_frame_limiter PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_link_options(test_frame_limiter PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(test_frame_limiter PRIVATE winmm)
add_test(NAME frame_limiter COMMAND test_frame_limiter)

# The ini lock's file rewrite against the shipped coduoplus.ini (upstream 1.6.9's
# tools/test_ini_lock.cpp): locked values in place, every other line byte for byte,
# idempotent, deleted keys re-appended, CRLF and LF kept, runtime values forced.
add_executable(test_ini_lock tests/test_ini_lock.cpp
               src/core/ini_lock.cpp src/core/logger.cpp src/core/game.cpp)
target_include_directories(test_ini_lock PRIVATE src)
target_compile_options(test_ini_lock PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_link_options(test_ini_lock PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME ini_lock COMMAND test_ini_lock "${CMAKE_SOURCE_DIR}/coduoplus.ini" "${CMAKE_BINARY_DIR}/testtmp")
set_tests_properties(frame_limiter PROPERTIES TIMEOUT 120)
