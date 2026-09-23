cmake_minimum_required(VERSION 3.25)
project(HoldForge VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HOLDFORGE_CORE_ONLY "Build standalone parser/planner tests without Geode" OFF)

if(HOLDFORGE_CORE_ONLY)
    enable_testing()
    add_executable(holdforge_tests tests/core_tests.cpp src/core/Slc.cpp src/core/Plan.cpp)
    target_include_directories(holdforge_tests PRIVATE src)
    add_test(NAME holdforge_core COMMAND holdforge_tests)
    return()
endif()

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK or build with GitHub Actions. For tests: -DHOLDFORGE_CORE_ONLY=ON")
endif()
add_library(${PROJECT_NAME} SHARED
    src/main.cpp src/HoldPopup.cpp src/Editor.cpp src/Diagnostics.cpp
    src/core/Slc.cpp src/core/Plan.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
