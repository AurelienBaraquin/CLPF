#include <clpf.h>
#include <stdio.h>

void task(void) {
    printf("task running\n");
}

int main(void)
{
    clpf(task);
}
