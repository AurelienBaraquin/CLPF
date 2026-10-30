# Benchmarks: `cmake --build <dir> --target bench` runs them (use a Release build).
# ctest runs a quick version, to keep them working.
add_executable(clpf_bench clpf_bench.c)
target_compile_options(clpf_bench PRIVATE -Wall -Wextra)
target_link_libraries(clpf_bench clpf)

add_custom_target(bench COMMAND clpf_bench DEPENDS clpf_bench USES_TERMINAL)

if(CLPF_BUILD_TESTS)
    add_test(NAME bench_quick COMMAND clpf_bench --quick)
    set_tests_properties(bench_quick PROPERTIES TIMEOUT 60)
endif()
