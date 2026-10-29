#include <api/registry.h>
#include <string.h>

// A name still pointing at an exited process is free: the exit cleanup may
// not have run yet when a replacement registers it again
static bool name_taken(const char *name) {
    Process *owner = process_lock(name_table_lookup(clpf.name_table, name));
    if (!owner)
        return false;

    bool taken = !owner->exit_notified;
    process_unlock(owner);
    return taken;
}

int clpf_register(const char *name, Pid pid) {
    if (name[0] == '\0' || strlen(name) >= NAME_MAX_LENGTH)
        return -1;

    int result = -1;
    pthread_mutex_lock(&clpf.table_lock);
    Process *process = process_lock(pid);
    if (process) {
        if (!process->exit_notified && process->registered_name[0] == '\0' && !name_taken(name) &&
            name_table_insert(clpf.name_table, name, pid) == 0) {
            strcpy(process->registered_name, name);
            result = 0;
        }
        process_unlock(process);
    }
    pthread_mutex_unlock(&clpf.table_lock);
    return result;
}

int clpf_unregister(const char *name) {
    pthread_mutex_lock(&clpf.table_lock);
    Pid pid = name_table_lookup(clpf.name_table, name);
    if (pid == INVALID_PID) {
        pthread_mutex_unlock(&clpf.table_lock);
        return -1;
    }

    name_table_remove(clpf.name_table, name);
    Process *process = process_lock(pid);
    if (process) {
        memset(process->registered_name, 0, NAME_MAX_LENGTH);
        process_unlock(process);
    }
    pthread_mutex_unlock(&clpf.table_lock);
    return 0;
}

Pid clpf_whereis(const char *name) {
    pthread_mutex_lock(&clpf.table_lock);
    Pid pid = name_table_lookup(clpf.name_table, name);
    pthread_mutex_unlock(&clpf.table_lock);
    return pid;
}
