#include <clpf_internal.h>
#include <stdio.h>

void task(void) {
    printf("task running\n");
}

int main(void)
{
    clpf_main(task);
}
