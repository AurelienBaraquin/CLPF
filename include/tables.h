#pragma once

#include <utils/pid.h>

// Deletes the tables of an ended process (main thread)
void tables_forget(Pid owner);
