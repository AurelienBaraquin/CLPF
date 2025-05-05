cmake_minimum_required(VERSION 3.15)
project(cbeam LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add -g3 flag for debugging
add_compile_options(-g3 -O0)



# Ajouter la bibliothèque TLSF
add_library(tlsf STATIC external/tlsf/tlsf.c)
target_include_directories(tlsf PUBLIC external/)

# Add libco (coroutines)
add_library(libco STATIC external/libco/libco.c)
target_include_directories(libco PUBLIC external/libco)



# Include directories
include_directories(include)
include_directories(external/ut)

# Source files
file(GLOB_RECURSE CBEAM_SOURCES
    src/*.c
)

file(GLOB_RECURSE ASM_SOURCES src/**/*.S)

add_library(cbeam STATIC ${CBEAM_SOURCES} ${ASM_SOURCES} ${EXTERNAL_SOURCES})
target_link_libraries(cbeam tlsf libco)

# Optional main binary
add_executable(cbeam_main main.c)
target_link_libraries(cbeam_main cbeam tlsf)
