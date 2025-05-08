typedef struct process_s Process;
typedef void (*Task)();

void __server_launcher__(Process *self, Task entry, int argc, char *argv[]);
