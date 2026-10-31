#include <utils/stack_guard.h>
#include <process.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define ALT_STACK_SIZE ((size_t)64 * 1024)

static __thread Process *running_process;
static __thread char alt_stack[ALT_STACK_SIZE];
static size_t page_size;

// Appends text without its NUL: the message is written with its length
static size_t append(char *buffer, size_t used, const char *text) {
    while (*text)
        buffer[used++] = *text++;
    return used;
}

static size_t append_number(char *buffer, size_t used, long value) {
    char digits[24];
    size_t count = 0;
    unsigned long magnitude = value < 0 ? -(unsigned long)value : (unsigned long)value;
    do {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0)
        buffer[used++] = '-';
    while (count)
        buffer[used++] = digits[--count];
    return used;
}

// Only async-signal-safe calls in here
static void on_segv(int sig, siginfo_t *info, void *context) {
    (void)context;
    Process *process = running_process;

    if (process && process->share_stack) {
        char *guard = process->share_stack->real_ptr;
        char *address = info->si_addr;
        if (address >= guard && address < guard + page_size) {
            char message[sizeof process->name + 160];
            size_t used = append(message, 0, "clpf: stack overflow in process '");
            used = append(message, used, process->name);
            used = append(message, used, "' (pid ");
            used = append_number(message, used, process->pid);
            used = append(message, used, "), give it a larger SpawnOptions.stack_size\n");
            ssize_t written = write(STDERR_FILENO, message, used);
            (void)written;
        }
    }

    // Crash as if there were no handler: the faulting instruction runs again
    signal(sig, SIG_DFL);
}

void stack_guard_install(void) {
    page_size = (size_t)sysconf(_SC_PAGESIZE);

    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_segv;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
}

void stack_guard_thread_init(void) {
    // Keep an alternate stack someone else installed (sanitizers do, and free it at thread exit)
    stack_t current;
    if (sigaltstack(NULL, &current) == 0 && !(current.ss_flags & SS_DISABLE))
        return;

    stack_t stack = {.ss_sp = alt_stack, .ss_size = ALT_STACK_SIZE, .ss_flags = 0};
    sigaltstack(&stack, NULL);
}

void stack_guard_set_running(Process *process) {
    running_process = process;
}

Process *stack_guard_running(void) {
    return running_process;
}
