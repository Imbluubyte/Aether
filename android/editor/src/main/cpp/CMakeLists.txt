cmake_minimum_required(VERSION 3.18)
project(aether_jni LANGUAGES C CXX)

get_filename_component(AETHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(AETHER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(AETHER_AUDIO_MINIAUDIO ON CACHE BOOL "" FORCE)
set(AETHER_RENDERER_GLES ON CACHE BOOL "" FORCE)
add_subdirectory(${AETHER_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)

add_library(aether_jni SHARED
  aether_jni.cpp
  ${AETHER_ROOT}/engine/src/platform/android/android_platform.cpp)
target_include_directories(aether_jni PRIVATE ${AETHER_ROOT}/engine/src/platform/android)
target_link_libraries(aether_jni PRIVATE aether_engine log android GLESv3 EGL)
