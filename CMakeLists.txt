cmake_minimum_required(VERSION 3.15)
project(clpf VERSION 0.2.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CLPF_SANITIZE "" CACHE STRING "Build with a sanitizer: thread or address (empty to disable)")
option(CLPF_TRACE "Log every process creation and destruction" OFF)
option(CLPF_BUILD_TESTS "Build the tests" ON)

add_compile_options(-fno-omit-frame-pointer)
if(CLPF_SANITIZE)
    add_compile_options(-fsanitize=${CLPF_SANITIZE})
    add_link_options(-fsanitize=${CLPF_SANITIZE})
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

# Vendored dependencies, prefixed so that installing them cannot clash with system libraries
add_library(clpf_tlsf STATIC external/tlsf/tlsf.c)
add_library(clpf_aco STATIC external/libaco/aco.c external/libaco/acosw.S)
add_library(clpf_stb_sprintf STATIC external/stb_sprintf/stb_sprintf.c)
target_include_directories(clpf_tlsf PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external>)

file(GLOB_RECURSE CLPF_SOURCES src/*.c src/*.S)

add_library(clpf STATIC ${CLPF_SOURCES})
add_library(clpf::clpf ALIAS clpf)
target_include_directories(clpf PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/clpf>)
target_compile_options(clpf PRIVATE -Wall -Wextra)
target_compile_definitions(clpf PRIVATE _GNU_SOURCE $<$<BOOL:${CLPF_TRACE}>:CLPF_TRACE>)
target_link_libraries(clpf PUBLIC clpf_tlsf clpf_aco clpf_stb_sprintf Threads::Threads)

# Demo
add_executable(clpf_demo main.c)
target_compile_options(clpf_demo PRIVATE -Wall -Wextra)
target_link_libraries(clpf_demo clpf)

# Install: libraries, headers under include/clpf (with the vendored headers they
# expose), a CMake package (find_package(clpf), target clpf::clpf) and a pkg-config file
install(TARGETS clpf clpf_tlsf clpf_aco clpf_stb_sprintf EXPORT clpfTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/clpf)
install(FILES external/tlsf/tlsf.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/clpf/tlsf)
install(FILES external/libaco/aco.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/clpf/libaco)
install(EXPORT clpfTargets NAMESPACE clpf:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clpf)

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/clpfConfig.cmake.in ${PROJECT_BINARY_DIR}/clpfConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clpf)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/clpfConfigVersion.cmake COMPATIBILITY SameMinorVersion)
install(FILES ${PROJECT_BINARY_DIR}/clpfConfig.cmake ${PROJECT_BINARY_DIR}/clpfConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clpf)

# The .pc file locates the prefix from its own directory, so --prefix at install time works
file(RELATIVE_PATH CLPF_PC_TO_PREFIX "/root/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/root")
configure_file(cmake/clpf.pc.in ${PROJECT_BINARY_DIR}/clpf.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/clpf.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(CLPF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
