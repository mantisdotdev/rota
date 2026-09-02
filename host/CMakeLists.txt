# Host build of the Rota sources (PRD §12): the same firmware/src tree with
# hal/sdl/ instead of hal/teensy/. Configure from the repo root:
#   cmake -S host -B build && cmake --build build
# Every executable lands directly in build/ so ./build/tests, ./build/simulator and
# ./build/render work as written in CLAUDE.md.
cmake_minimum_required(VERSION 3.20)
project(rota_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

cmake_path(SET REPO_ROOT NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FIRMWARE_SRC "${REPO_ROOT}/firmware/src")

add_compile_options(-Wall -Wextra)

# core: the portable layers. Never hal/teensy/, never firmware/src/main.cpp.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  "${FIRMWARE_SRC}/engine/*.cpp"
  "${FIRMWARE_SRC}/sound/*.cpp"
  "${FIRMWARE_SRC}/ui/*.cpp"
  "${FIRMWARE_SRC}/io/*.cpp"
  "${FIRMWARE_SRC}/app/*.cpp")
if(NOT CORE_SOURCES)
  message(FATAL_ERROR "No .cpp files under ${FIRMWARE_SRC}/{engine,sound,ui,io,app}")
endif()
add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core PUBLIC "${FIRMWARE_SRC}")
# The Teensy builder compiles without exceptions or RTTI; matching it here means code
# that builds on the host also builds on the device.
target_compile_options(core PRIVATE -fno-exceptions -fno-rtti)

# hal_sdl: the host HAL (D-016). SDL2main is not needed on macOS or Linux.
find_package(SDL2 REQUIRED CONFIG)
file(GLOB_RECURSE HAL_SDL_SOURCES CONFIGURE_DEPENDS "${FIRMWARE_SRC}/hal/sdl/*.cpp")
if(NOT HAL_SDL_SOURCES)
  message(FATAL_ERROR "No .cpp files under ${FIRMWARE_SRC}/hal/sdl")
endif()
add_library(hal_sdl STATIC ${HAL_SDL_SOURCES})
target_include_directories(hal_sdl PUBLIC "${FIRMWARE_SRC}")
target_link_libraries(hal_sdl PUBLIC SDL2::SDL2)
if(APPLE)
  # audio_sdl.cpp asks the output device for 128-frame IO buffers (D-090).
  target_link_libraries(hal_sdl PUBLIC "-framework CoreAudio")
endif()
# The simulator's "SD card" is a directory under out/, which git ignores.
target_compile_definitions(hal_sdl PRIVATE ROTA_STORAGE_DIR="${REPO_ROOT}/out/sdcard")

# The simulator reads the kit's WAVs through the render tool's loader (host-only code).
add_executable(simulator "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_link_libraries(simulator PRIVATE hal_sdl offline)

# tests: doctest on the host. No SDL, so it runs headless in CI; tests/hal_fake.cpp
# stands in for the HAL so app/ runs under a scripted clock.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/tests/*.cpp")
add_executable(tests ${TEST_SOURCES})
target_include_directories(tests SYSTEM PRIVATE "${REPO_ROOT}/tests")
target_link_libraries(tests PRIVATE offline)

# offline: the share-code renderer behind tools/render (D-082). A library so the tests
# exercise the same code without spawning the tool. The kit folder is baked in so the
# tool needs no argument for it.
add_library(offline STATIC "${REPO_ROOT}/tools/render/offline.cpp")
target_include_directories(offline PUBLIC "${REPO_ROOT}/tools")
target_compile_definitions(offline PUBLIC ROTA_KITS_DIR="${REPO_ROOT}/spec/kits")
target_link_libraries(offline PUBLIC core)

add_executable(render "${REPO_ROOT}/tools/render/main.cpp")
target_link_libraries(render PRIVATE offline)
