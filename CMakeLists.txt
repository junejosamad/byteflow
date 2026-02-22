cmake_minimum_required(VERSION 3.10)
project(OpenEDA VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Setup include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Automatically find all C++ source files in the src/ directory and subdirectories
file(GLOB_RECURSE SOURCES "src/*.cpp")

# --- PHASE 3: GUI Dependencies (GLFW & OpenGL) ---
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
# Disable GLFW bloat
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Define the executable
add_executable(open_eda ${SOURCES})

# Link GUI libraries and OpenMP
target_link_libraries(open_eda PRIVATE glfw OpenGL::GL)

# --- PREPARING FOR PHASE 2: MULTITHREADING ---
# Find OpenMP to allow parallel routing and placement in the future
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(open_eda PUBLIC OpenMP::OpenMP_CXX)
    if(MSVC)
        target_compile_options(open_eda PRIVATE /openmp:llvm)
    endif()
    message(STATUS "OpenMP found: Parallel processing enabled.")
else()
    message(WARNING "OpenMP not found: Running in single-threaded mode.")
endif()
