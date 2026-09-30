cmake_minimum_required(VERSION 3.16)
project(ZenTetris CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(Threads REQUIRED)

# Audio decoding, analysis and song planning, shared by the game and the analysis viewer.
add_library(zencore STATIC
  src/analysis.cpp
  src/audio.cpp
  src/library.cpp
  src/songplan.cpp
  src/youtube.cpp
  src/third_party_impl.cpp
)
target_include_directories(zencore PUBLIC src third_party)
target_link_libraries(zencore PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

# The game.
add_executable(zentetris
  src/main.cpp
  src/game.cpp
  src/input.cpp
  src/theme.cpp
  src/renderer.cpp
)
target_link_libraries(zentetris PRIVATE zencore glfw GLEW::GLEW OpenGL::GL)

# zenscope: shows what the analysis hears in a song (structure, scene levels, pulses, spectrum, beats).
add_executable(zenscope src/zenscope.cpp)
target_link_libraries(zenscope PRIVATE zencore glfw GLEW::GLEW OpenGL::GL)

foreach(t zencore zentetris zenscope)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${t} PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
  endif()
endforeach()
