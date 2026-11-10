cmake_minimum_required(VERSION 3.18)
project(aether_player LANGUAGES C CXX)

get_filename_component(AETHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(AETHER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(AETHER_AUDIO_MINIAUDIO ON CACHE BOOL "" FORCE)
set(AETHER_RENDERER_GLES ON CACHE BOOL "" FORCE)
add_subdirectory(${AETHER_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)

set(APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${APP_GLUE}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${APP_GLUE})

add_library(aether_player SHARED
  player_main.cpp
  ${AETHER_ROOT}/engine/src/platform/android/android_platform.cpp)
target_include_directories(aether_player PRIVATE ${AETHER_ROOT}/engine/src/platform/android)
# The glue's ANativeActivity_onCreate must be exported from the shared library.
set_target_properties(aether_player PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
target_link_libraries(aether_player PRIVATE aether_engine native_app_glue log android GLESv3 EGL)
