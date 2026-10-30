#pragma once

#include <utils/pid.h>

// Removes an ended process from every group it joined (main thread)
void groups_forget(Pid pid);
