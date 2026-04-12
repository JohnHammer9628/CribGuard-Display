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

# ---- GStreamer (optional, for camera streaming) ----
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(GSTREAMER gstreamer-1.0)
  pkg_check_modules(GSTREAMER_BASE gstreamer-base-1.0)
  pkg_check_modules(GSTREAMER_APP gstreamer-app-1.0)
  if (GSTREAMER_FOUND AND GSTREAMER_BASE_FOUND AND GSTREAMER_APP_FOUND)
    message(STATUS "GStreamer found: ${GSTREAMER_VERSION}")
  else()
    message(WARNING "GStreamer not found - camera streaming will be disabled")
  endif()
else()
  message(WARNING "pkg-config not found - skipping GStreamer detection")
endif()

# ---- OpenCV (optional, for thermal processing/streaming service) ----
find_package(OpenCV QUIET COMPONENTS core imgproc highgui videoio)
if (OpenCV_FOUND)
  message(STATUS "OpenCV found: ${OpenCV_VERSION}")
else()
  message(WARNING "OpenCV not found - native lepton monitor target may be disabled")
endif()

# ---- libcurl (optional, for HTTP API calls) ----
find_package(CURL)
if (CURL_FOUND)
  message(STATUS "libcurl found: ${CURL_VERSION_STRING}")
else()
  message(WARNING "libcurl not found - camera control API will be disabled")
endif()

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

# Fetch LVGL so we can exclude ASM sources before configure.
FetchContent_GetProperties(lvgl)
if (NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()

# ---- Workaround: remove LVGL assembly sources (.S) before build ----
# LVGL's CMake globs *.S unconditionally. These optimized Helium/NEON sources
# fail on aarch64 (Pi 5) and x86 (Windows). set_source_files_properties doesn't
# carry into the subdirectory scope, so we delete the files outright.
file(GLOB_RECURSE LVGL_ASM_SOURCES "${lvgl_SOURCE_DIR}/src/*.S")
if (LVGL_ASM_SOURCES)
  file(REMOVE ${LVGL_ASM_SOURCES})
  message(STATUS "Removed LVGL .S assembly files to avoid assembler errors")
endif()

# Build LVGL now
add_subdirectory(${lvgl_SOURCE_DIR} ${lvgl_BINARY_DIR})

# ---- Patch LVGL build flags (important on MSVC) ----
# - Enable SDL backend inside LVGL build
# - Clear LV_ATTRIBUTE_EXTERN_DATA to avoid dllimport/dllexport mismatch errors
# - Link SDL to LVGL so its SDL driver compiles
target_compile_definitions(lvgl PRIVATE
  LV_USE_SDL=1
  LV_ATTRIBUTE_EXTERN_DATA=
  LV_CONF_INCLUDE_SIMPLE
  LV_LVGL_H_INCLUDE_SIMPLE
  # Force software renderer to avoid native ASM paths on Pi
  LV_USE_DRAW_SW_ASM=LV_DRAW_SW_ASM_NONE
  LV_USE_NATIVE_HELIUM_ASM=0
)

target_link_libraries(lvgl PRIVATE
  $<$<TARGET_EXISTS:SDL2::SDL2main>:SDL2::SDL2main>
  $<$<TARGET_EXISTS:SDL2::SDL2>:SDL2::SDL2>
)

# ---- Your app ----
add_executable(crib_guard_pi
  platforms/pi-sdl/main.cpp
  src/logging.cpp
  src/config.cpp
  src/baby_pi_api.cpp
  src/camera_rx_gst.cpp
  src/ui_app.cpp
  src/ui/common.cpp
  src/ui/state.cpp
  src/ui/dashboard.cpp
  src/ui/library.cpp
  src/ui/lullabies.cpp
  src/ui/settings.cpp
  src/ui/camera.cpp
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

# Link GStreamer if found
if (GSTREAMER_FOUND AND GSTREAMER_BASE_FOUND AND GSTREAMER_APP_FOUND)
  target_include_directories(crib_guard_pi PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_BASE_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
  )
  target_link_libraries(crib_guard_pi PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_BASE_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
  )
  target_link_directories(crib_guard_pi PRIVATE
    ${GSTREAMER_LIBRARY_DIRS}
    ${GSTREAMER_BASE_LIBRARY_DIRS}
    ${GSTREAMER_APP_LIBRARY_DIRS}
  )
  message(STATUS "GStreamer support enabled")
endif()

# Link libcurl if found
if (CURL_FOUND)
  target_include_directories(crib_guard_pi PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(crib_guard_pi PRIVATE ${CURL_LIBRARIES})
  message(STATUS "libcurl support enabled")
endif()

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

# ---- Native Lepton monitor service (Linux, optional deps) ----
if (PKG_CONFIG_FOUND)
  pkg_check_modules(LIBUVC QUIET IMPORTED_TARGET libuvc)
  pkg_check_modules(LIBZMQ QUIET IMPORTED_TARGET libzmq)
endif()

option(BUILD_LEPTON_MONITOR "Build native Lepton monitor service (libuvc + OpenCV + ZeroMQ)" ON)

if (BUILD_LEPTON_MONITOR)
  if (UNIX AND OpenCV_FOUND AND LIBUVC_FOUND AND LIBZMQ_FOUND AND GSTREAMER_FOUND)
    add_executable(lepton_monitor_service
      baby-pi/lepton_monitor_service.cpp
    )

    target_include_directories(lepton_monitor_service PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(lepton_monitor_service PRIVATE
      ${OpenCV_LIBS}
      PkgConfig::LIBUVC
      PkgConfig::LIBZMQ
      ${GSTREAMER_LIBRARIES}
      ${GSTREAMER_BASE_LIBRARIES}
      ${GSTREAMER_APP_LIBRARIES}
    )

    target_include_directories(lepton_monitor_service PRIVATE
      ${GSTREAMER_INCLUDE_DIRS}
      ${GSTREAMER_BASE_INCLUDE_DIRS}
      ${GSTREAMER_APP_INCLUDE_DIRS}
    )

    target_link_directories(lepton_monitor_service PRIVATE
      ${GSTREAMER_LIBRARY_DIRS}
      ${GSTREAMER_BASE_LIBRARY_DIRS}
      ${GSTREAMER_APP_LIBRARY_DIRS}
    )

    if (MSVC)
      target_compile_options(lepton_monitor_service PRIVATE /W3 /permissive-)
    else()
      target_compile_options(lepton_monitor_service PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    set_target_properties(lepton_monitor_service PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    message(STATUS "Native Lepton monitor target enabled: lepton_monitor_service")
  else()
    message(WARNING
      "Skipping lepton_monitor_service. Requirements: UNIX + OpenCV + libuvc + libzmq + gstreamer-1.0"
    )
  endif()
endif()
