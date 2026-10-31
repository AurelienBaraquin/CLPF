# Small runnable examples; ctest runs each one and expects exit code 0
foreach(example ping_pong supervision timers links)
    add_executable(example_${example} ${example}.c)
    target_compile_options(example_${example} PRIVATE -Wall -Wextra)
    target_link_libraries(example_${example} clpf)
    if(CLPF_BUILD_TESTS)
        add_test(NAME example_${example} COMMAND example_${example})
        set_tests_properties(example_${example} PROPERTIES TIMEOUT 20)
    endif()
endforeach()

# A complete program: a TCP chat server, and a load test of it run by ctest
add_executable(example_chat chat/main.c chat/server.c chat/load.c)
target_compile_options(example_chat PRIVATE -Wall -Wextra)
target_link_libraries(example_chat clpf)
if(CLPF_BUILD_TESTS)
    add_test(NAME example_chat_selftest COMMAND example_chat selftest 200 10 20)
    # Heavy: alone, so that it does not skew the tests that measure time
    set_tests_properties(example_chat_selftest PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
