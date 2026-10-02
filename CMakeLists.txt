cmake_minimum_required(VERSION 3.16)
project(hls_packager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

add_executable(hls_packager
  src/main.cpp
  src/process.cpp
  src/ffmpeg_encoder.cpp
  src/hls_playlist_writer.cpp
  src/packager.cpp
)
target_include_directories(hls_packager PRIVATE src)
target_link_libraries(hls_packager PRIVATE Threads::Threads)
target_compile_options(hls_packager PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

# GCC < 9 needs an explicit link against stdc++fs for <filesystem>.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(hls_packager PRIVATE stdc++fs)
endif()
