# FIGHTO - engine-free build. No game engine is used: the game links only
# against the operating system's windowing/OpenGL/audio libraries.
cmake_minimum_required(VERSION 3.16)
project(FIGHTO CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(FIGHTO_COMMON_FLAGS -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function)

file(GLOB FIGHTO_CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/core/*.cpp
  ${CMAKE_SOURCE_DIR}/src/gfx/*.cpp
  ${CMAKE_SOURCE_DIR}/src/game/*.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
  ${CMAKE_SOURCE_DIR}/src/audio/*.cpp)

if(WIN32)
  set(FIGHTO_PLATFORM_SOURCES
    ${CMAKE_SOURCE_DIR}/src/platform/platform_win32.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/platform_desktop_common.cpp
    ${CMAKE_SOURCE_DIR}/windows/fighto.rc)
  add_executable(fighto WIN32 ${FIGHTO_CORE_SOURCES} ${FIGHTO_PLATFORM_SOURCES})
  target_link_libraries(fighto opengl32 winmm gdi32 user32)
  target_link_options(fighto PRIVATE -static -static-libgcc -static-libstdc++)
  set_target_properties(fighto PROPERTIES OUTPUT_NAME "FIGHTO")
else()
  set(FIGHTO_PLATFORM_SOURCES
    ${CMAKE_SOURCE_DIR}/src/platform/platform_x11.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/platform_desktop_common.cpp)
  add_executable(fighto ${FIGHTO_CORE_SOURCES} ${FIGHTO_PLATFORM_SOURCES})
  target_link_libraries(fighto X11 GL dl pthread m)
endif()
target_compile_options(fighto PRIVATE ${FIGHTO_COMMON_FLAGS})

# Headless unit tests (no window / GL needed)
if(NOT WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/tests/unit_tests.cpp)
  add_executable(fighto_tests
    ${CMAKE_SOURCE_DIR}/tests/unit_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/core/base.cpp
    ${CMAKE_SOURCE_DIR}/src/core/vmath.cpp
    ${CMAKE_SOURCE_DIR}/src/core/noise.cpp
    ${CMAKE_SOURCE_DIR}/src/game/save.cpp
    ${CMAKE_SOURCE_DIR}/src/game/script.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/platform_desktop_common.cpp)
  target_compile_options(fighto_tests PRIVATE ${FIGHTO_COMMON_FLAGS} -DFIGHTO_UNIT_TESTS=1)
  target_link_libraries(fighto_tests m)
endif()
