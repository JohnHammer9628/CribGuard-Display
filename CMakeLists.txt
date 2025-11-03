cmake_minimum_required(VERSION 3.16)
project(crib_guard_pi LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(\/config)

include(FetchContent)

FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.0
)
FetchContent_MakeAvailable(lvgl)

FetchContent_Declare(
  lv_drivers
  GIT_REPOSITORY https://github.com/lvgl/lv_drivers.git
  GIT_TAG master
)
FetchContent_MakeAvailable(lv_drivers)

find_package(SDL2 REQUIRED)
include_directories(\)

add_executable(crib_guard_pi
    platforms/pi-sdl/main.cpp
    \/sdl/sdl.c
)

target_include_directories(crib_guard_pi PRIVATE
    \
    \
    ui
)

target_link_libraries(crib_guard_pi
    lvgl
    \
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
endif()
