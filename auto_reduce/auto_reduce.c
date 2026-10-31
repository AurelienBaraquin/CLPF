// Automatic reductions: link clpf::auto_reduce, and the program's code, built
// with -finstrument-functions, calls these hooks at the entry and exit of
// each of its functions. Each call costs a reduction, so a long computation
// gives the CPU away without REDUCE, as long as it calls functions.
// This file itself is built without the flag.
#include <clpf_internal.h>
#include <utils/stack_guard.h>

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *function, void *call_site);
__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *function, void *call_site);

void __cyg_profile_func_enter(void *function, void *call_site) {
    (void)function;
    (void)call_site;
    // Outside processes (the main thread, threads of the program), nothing to do
    Process *process = stack_guard_running();
    if (process && clpf_preemption_allowed())
        clpf_reduce(process, 1);
}

void __cyg_profile_func_exit(void *function, void *call_site) {
    (void)function;
    (void)call_site;
}
