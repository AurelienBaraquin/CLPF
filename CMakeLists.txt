cmake_minimum_required(VERSION 3.15)
project(cbeam LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add -g3 flag for debugging
add_compile_options(-g3 -Wall -Wextra -Wpedantic -O0)

# Ajouter la bibliothèque TLSF
add_library(tlsf STATIC external/tlsf/tlsf.c)
target_include_directories(tlsf PUBLIC external/)

# Include directories
include_directories(include)
include_directories(external/ut)

# Source files
file(GLOB_RECURSE CBEAM_SOURCES
    src/*.c
)

add_library(cbeam STATIC ${CBEAM_SOURCES} ${EXTERNAL_SOURCES})
target_link_libraries(cbeam tlsf)

# Optional main binary
add_executable(cbeam_main main.c)
target_link_libraries(cbeam_main cbeam tlsf)
