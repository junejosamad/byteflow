cmake_minimum_required(VERSION 3.10)
project(OpenEDA VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Setup include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Automatically find all C++ source files in the src/ directory and subdirectories
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Define the executable
add_executable(open_eda ${SOURCES})

# --- PREPARING FOR PHASE 2: MULTITHREADING ---
# Find OpenMP to allow parallel routing and placement in the future
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(open_eda PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP found: Parallel processing enabled.")
else()
    message(WARNING "OpenMP not found: Running in single-threaded mode.")
endif()
