cmake_minimum_required(VERSION 3.16)
project(crib_guard_pi LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(${CMAKE_SOURCE_DIR}/config)
add_subdirectory(extern/lvgl)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
add_executable(crib_guard_pi
    platforms/pi-sdl/main.cpp
    extern/lv_drivers/sdl/sdl.c
)
target_include_directories(crib_guard_pi PRIVATE
    extern/lvgl
    extern/lv_drivers
    ui
)
target_link_libraries(crib_guard_pi
    lvgl
    ${SDL2_LIBRARIES}
)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
endif()