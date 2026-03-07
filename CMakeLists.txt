cmake_minimum_required(VERSION 3.10)
project(OpenEDA VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Setup include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

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

# --- PHASE 4: Python Scripting Shell (Pybind11) ---
option(BUILD_PYTHON_MODULE "Build OpenEDA as a Python module" OFF)

if(BUILD_PYTHON_MODULE)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)

    # Remove main.cpp so there is no duplicate main()
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    
    # Build as a dynamic python library
    pybind11_add_module(open_eda ${SOURCES})

    # Automatically copy the compiled .pyd / .so to the project root for testing
    add_custom_command(TARGET open_eda POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:open_eda>
        ${CMAKE_SOURCE_DIR}/$<TARGET_FILE_NAME:open_eda>
    )
else()
    # Remove PyBindings.cpp (requires pybind11 headers)
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/scripting/PyBindings.cpp")

    # Define the standalone executable
    add_executable(open_eda ${SOURCES})
endif()

# Link GUI libraries
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
