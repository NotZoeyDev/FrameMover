cmake_minimum_required(VERSION 3.20)
project(frame-mover CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()
option(FRAME_MOVER_BUILD_APP "Build the SteamVR application" ON)
include(CTest)

# The model and pure tests can be built without the Frame's graphics/VR dependencies.
add_library(playspace STATIC src/playspace.cpp)
target_include_directories(playspace PUBLIC src)
target_compile_definitions(playspace PUBLIC $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>)
target_compile_definitions(playspace PUBLIC $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

if(FRAME_MOVER_BUILD_APP)

# OpenVR headers and runtime library as shipped with SteamVR on the Frame.
set(OPENVR_HEADERS "/opt/steamvr/tools/hellovr_vulkan_linux/src/openvr/headers"
    CACHE PATH "Directory containing openvr.h")
set(OPENVR_LIB "/opt/steamvr/bin/linuxarm64/libopenvr_api.so"
    CACHE FILEPATH "libopenvr_api.so to link against")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)

find_package(Threads REQUIRED)

add_executable(frame-mover
  src/main.cpp
  src/actions.cpp
  src/app.cpp
  src/backup.cpp
  src/chaperone.cpp
  src/config.cpp
  src/i18n.cpp
  src/install.cpp
  src/panel.cpp
  src/ui.cpp
  src/vk_texture.cpp)
target_include_directories(frame-mover PRIVATE src ${OPENVR_HEADERS})
target_compile_options(frame-mover PRIVATE -Wall -Wextra)
target_link_libraries(frame-mover PRIVATE playspace PkgConfig::CAIRO PkgConfig::VULKAN ${OPENVR_LIB} Threads::Threads)
# Keep unreachable code/data out of release binaries; retain debug symbols in Debug builds.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(target frame-mover playspace)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
  endforeach()
  target_link_options(frame-mover PRIVATE -Wl,--gc-sections $<$<CONFIG:MinSizeRel>:-s>)
endif()
include(CheckIPOSupported)
check_ipo_supported(RESULT have_ipo)
if(have_ipo)
  set_target_properties(frame-mover playspace PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
endif()
# Vulkan create-info structs are conventionally brace-initialised with only sType set.
set_source_files_properties(src/vk_texture.cpp PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
get_filename_component(OPENVR_LIB_DIR "${OPENVR_LIB}" DIRECTORY)
set_target_properties(frame-mover PROPERTIES
  BUILD_RPATH "${OPENVR_LIB_DIR}" INSTALL_RPATH "${OPENVR_LIB_DIR}")

# The program finds its action manifest and default bindings in a "data" directory next to it.
add_custom_command(TARGET frame-mover POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:frame-mover>/data)
endif()

# Unit tests (no SteamVR needed): ctest --test-dir build --output-on-failure
if(BUILD_TESTING)
  foreach(test playspace pixel bindings panel_action config i18n)
    add_executable(${test}_tests tests/${test}_tests.cpp)
    target_link_libraries(${test}_tests PRIVATE playspace)
    add_test(NAME ${test} COMMAND ${test}_tests)
  endforeach()
  target_sources(config_tests PRIVATE src/config.cpp)
  target_sources(i18n_tests PRIVATE src/i18n.cpp src/config.cpp)
  set_tests_properties(bindings PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
