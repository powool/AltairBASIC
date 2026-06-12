cmake_minimum_required(VERSION 3.5)

project(i8080tools)

set(CMAKE_CXX_STANDARD 20)

# https://cmake.org/cmake/help/latest/module/FetchContent.html
include(FetchContent)
FetchContent_Declare(
  i8080-core
  GIT_REPOSITORY https://github.com/begoon/i8080-core.git
  GIT_TAG        6d197e6c4183bf3a48a9eb730be54972fbb0db11
  #  SOURCE_SUBDIR  i8080-core
)
FetchContent_MakeAvailable(i8080-core)

if("${CMAKE_BUILD_TYPE}" MATCHES "")
	set(CMAKE_BUILD_TYPE "Debug")
	message("Configuring for '${CMAKE_BUILD_TYPE}'")
	message("Use cmake -DCMAKE_BUILD_TYPE=Release to build for release.")
endif()

INCLUDE_DIRECTORIES(${i8080-core_SOURCE_DIR})

set_source_files_properties(i8080_hal.c PROPERTIES LANGUAGE CXX )
set_source_files_properties(${i8080-core_SOURCE_DIR}/i8080.c PROPERTIES LANGUAGE CXX )

add_executable(i8080tool i8080tool.cpp Instructions.cpp i8080_hal.c ${i8080-core_SOURCE_DIR}/i8080.c)

add_custom_target(
	cleanall
	COMMAND make clean
	COMMAND rm -rf CMakeCache.txt CMakeFiles Makefile cmake_install.cmake _deps
)
