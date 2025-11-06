cmake_minimum_required(VERSION 3.21)

project(CribGuardPi
  VERSION 0.1.0
  LANGUAGES C CXX)

# ---- Language standards ----
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- vcpkg/SDL2 ----
# Ensure you've installed: vcpkg install sdl2:x64-windows
find_package(SDL2 CONFIG REQUIRED)

# ---- Fetch LVGL ----
include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.0
)
# Tell LVGL where your config is BEFORE it configures
# LVGL's CMake respects this cache variable and adds the define internally.
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/lv_conf.h" CACHE STRING "LVGL config header" FORCE)
# Also make includes simple inside LVGL build
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
# Build LVGL now
FetchContent_MakeAvailable(lvgl)

# ---- Patch LVGL build flags (important on MSVC) ----
# - Enable SDL backend inside LVGL build
# - Clear LV_ATTRIBUTE_EXTERN_DATA to avoid dllimport/dllexport mismatch errors
# - Link SDL to LVGL so its SDL driver compiles
target_compile_definitions(lvgl PRIVATE
  LV_USE_SDL=1
  LV_ATTRIBUTE_EXTERN_DATA=
  LV_CONF_INCLUDE_SIMPLE
  LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(lvgl PRIVATE
  $<$<TARGET_EXISTS:SDL2::SDL2main>:SDL2::SDL2main>
  $<$<TARGET_EXISTS:SDL2::SDL2>:SDL2::SDL2>
)

# ---- Your app ----
add_executable(crib_guard_pi
  platforms/pi-sdl/main.cpp
)

# Include paths you commonly use
target_include_directories(crib_guard_pi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/app
  ${CMAKE_CURRENT_SOURCE_DIR}/apps
  ${CMAKE_CURRENT_SOURCE_DIR}/examples
  ${CMAKE_CURRENT_SOURCE_DIR}/platforms
  ${CMAKE_CURRENT_SOURCE_DIR}/platforms/pi-sdl
  ${lvgl_SOURCE_DIR}
  ${lvgl_BINARY_DIR}
)

# App-side defines (safe to duplicate for clarity)
target_compile_definitions(crib_guard_pi PRIVATE
  LV_CONF_INCLUDE_SIMPLE
  LV_LVGL_H_INCLUDE_SIMPLE
  LV_USE_SDL=1
  LV_ATTRIBUTE_EXTERN_DATA=
)

target_link_libraries(crib_guard_pi PRIVATE
  lvgl
  $<$<TARGET_EXISTS:SDL2::SDL2main>:SDL2::SDL2main>
  $<$<TARGET_EXISTS:SDL2::SDL2>:SDL2::SDL2>
)

if (WIN32)
  target_link_libraries(crib_guard_pi PRIVATE winmm imm32 version)
endif()

# Warnings (optional)
if (MSVC)
  target_compile_options(crib_guard_pi PRIVATE /W3 /permissive-)
else()
  target_compile_options(crib_guard_pi PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Output exe to build dir root
set_target_properties(crib_guard_pi PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
