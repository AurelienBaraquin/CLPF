cmake_minimum_required(VERSION 3.15)
project(cbeam LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add -g3 flag for debugging
add_compile_options(-g3 -O0)



# Add tlsf (memory allocator)
add_library(tlsf STATIC external/tlsf/tlsf.c)

# Add libaco (coroutines)
add_library(libaco STATIC external/libaco/aco.c external/libaco/acosw.S)

# Add stb_sprintf (string formatting)
add_library(stb_sprintf STATIC external/stb_sprintf/stb_sprintf.c)

target_include_directories(tlsf PUBLIC external/)


# Include directories
include_directories(include)
include_directories(external/ut)

# Source files
file(GLOB_RECURSE CBEAM_SOURCES
    src/*.c
)

file(GLOB_RECURSE ASM_SOURCES src/**/*.S)

add_library(cbeam STATIC ${CBEAM_SOURCES} ${ASM_SOURCES} ${EXTERNAL_SOURCES})
target_link_libraries(cbeam tlsf libaco stb_sprintf)




# Optional main binary
add_executable(cbeam_main main.c)
target_link_libraries(cbeam_main cbeam)
