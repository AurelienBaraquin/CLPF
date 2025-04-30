#pragma once

#include <clpf_internal.h>

// Sorting function for the scheduler table : First = More processes -> last = Less processes
void sch_mg_sort(void);

Pid sch_mg_get_less_process_scheduler(void);
