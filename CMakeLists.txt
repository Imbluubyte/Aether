cmake_minimum_required(VERSION 3.16)
project(Aether LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(AETHER_BUILD_TOOLS "Build aether_run and the C++ tests" ON)
option(AETHER_AUDIO_MINIAUDIO "Enable the miniaudio backend" OFF)
option(AETHER_RENDERER_GLES "Enable the OpenGL ES renderer (Android / Switch)" OFF)

add_subdirectory(engine)

if(AETHER_BUILD_TOOLS)
  add_subdirectory(tools/aether_run)
  enable_testing()
  add_subdirectory(tests)
endif()
