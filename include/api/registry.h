#pragma once

#include <clpf_internal.h>

// Process registry: names made of [A-Za-z0-9_:], one name per process,
// released automatically when the process exits.

// Gives pid the name, returns 0, or -1 if the name is invalid or taken,
// pid does not exist or already has a name
#define REGISTER(name, pid) clpf_register(name, pid)

// Returns 0, or -1 if the name was not registered
#define UNREGISTER(name) clpf_unregister(name)

// Returns the pid registered under the name, or INVALID_PID
#define WHEREIS(name) clpf_whereis(name)

int clpf_register(const char *name, Pid pid);
int clpf_unregister(const char *name);
Pid clpf_whereis(const char *name);
