# The Aether engine core. Platform backends (android / switch) are compiled by their own targets and
# link against this library; nothing here depends on Kotlin, the JVM or any OS windowing API.
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
# Compiled as C++ on purpose: errors become exceptions and unwind C++ frames correctly.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(aether_lua STATIC ${LUA_SOURCES})
target_include_directories(aether_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)
target_compile_definitions(aether_lua PRIVATE LUA_USE_POSIX)
if(NOT MSVC)
  target_compile_options(aether_lua PRIVATE -w)
endif()

set(AETHER_SOURCES
  src/core/log.cpp src/core/filesystem.cpp src/core/timers.cpp src/core/resources.cpp src/core/engine.cpp
  src/scene/scene.cpp
  src/physics/physics.cpp
  src/input/input.cpp
  src/platform/platform_names.cpp
  src/render/sprite_batch.cpp src/render/software_renderer.cpp src/render/font.cpp src/render/gles_renderer.cpp
  src/audio/miniaudio_impl.cpp src/audio/miniaudio_audio.cpp
  src/script/lua_vm.cpp src/script/bind_core.cpp src/script/bind_node.cpp src/script/bind_systems.cpp src/script/prelude.cpp)

add_library(aether_engine STATIC ${AETHER_SOURCES})
target_include_directories(aether_engine
  PUBLIC include
  PRIVATE src/script third_party/stb third_party/miniaudio)
target_link_libraries(aether_engine PUBLIC aether_lua)
if(NOT MSVC)
  target_compile_options(aether_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(AETHER_AUDIO_MINIAUDIO)
  target_compile_definitions(aether_engine PUBLIC AETHER_AUDIO_MINIAUDIO)
  find_package(Threads REQUIRED)
  target_link_libraries(aether_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
if(AETHER_RENDERER_GLES)
  target_compile_definitions(aether_engine PUBLIC AETHER_RENDERER_GLES)
endif()
