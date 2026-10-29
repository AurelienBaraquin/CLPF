#pragma once

typedef struct process_s Process;
typedef void (*Task)();

void clpf_server_launcher(Process *self, Task entry, int argc, char *argv[]);
