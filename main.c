#include <clpf.h>
#include <stdio.h>

void task(void) {
    printf("task running\n");
}

int main(void)
{
    Process *process = process_create(task, PROCESS_PRIORITY_HIGH);
    if (!process) {
        return -1; // Failed to create process
    }
    process_destroy(process);
    return 0;
}
