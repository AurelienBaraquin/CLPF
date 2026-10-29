cmake_minimum_required(VERSION 3.15)
project(cbeam LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CLPF_SANITIZE "" CACHE STRING "Build with a sanitizer: thread or address (empty to disable)")

add_compile_options(-fno-omit-frame-pointer)
if(CLPF_SANITIZE)
    add_compile_options(-fsanitize=${CLPF_SANITIZE})
    add_link_options(-fsanitize=${CLPF_SANITIZE})
endif()

# Add tlsf (memory allocator)
add_library(tlsf STATIC external/tlsf/tlsf.c)

# Add libaco (coroutines)
add_library(libaco STATIC external/libaco/aco.c external/libaco/acosw.S)

# Add stb_sprintf (string formatting)
add_library(stb_sprintf STATIC external/stb_sprintf/stb_sprintf.c)

target_include_directories(tlsf PUBLIC external/)

# Source files
file(GLOB_RECURSE CBEAM_SOURCES src/*.c src/*.S)

add_library(cbeam STATIC ${CBEAM_SOURCES})
target_include_directories(cbeam PUBLIC include external)
target_compile_options(cbeam PRIVATE -Wall -Wextra)
target_link_libraries(cbeam PUBLIC tlsf libaco stb_sprintf pthread)

# Optional main binary
add_executable(cbeam_main main.c)
target_compile_options(cbeam_main PRIVATE -Wall -Wextra)
target_link_libraries(cbeam_main cbeam)

enable_testing()
add_subdirectory(tests)
