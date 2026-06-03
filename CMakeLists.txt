# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)
set(GIT_FOUND FALSE CACHE BOOL "" FORCE)
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pio_version)