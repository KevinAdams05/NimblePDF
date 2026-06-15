# Cross-platform (Qt) build for NimblePDF — Linux + Windows.
#
# The Haiku app keeps its own Generic Makefile under source/; this CMake tree
# builds the desktop port only:
#
#   core/   toolkit-free library (poppler-cpp renderer + document model)
#   qt/     Qt 6 frontend (Linux + Windows)
#
#   cmake -S . -B build-qt && cmake --build build-qt -j
cmake_minimum_required(VERSION 3.21)
project(NimblePDF VERSION 2.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(core)
add_subdirectory(qt)
