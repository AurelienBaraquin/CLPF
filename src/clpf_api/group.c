#include <api/group.h>
#include <groups.h>
#include <utils/hash_map.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name; // The key of the group in groups
    Pid *members;     // In join order
    size_t count;
    size_t capacity;
} Group;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static HashMap *groups; // Guarded by lock: name -> Group; empty groups are removed

static bool valid_name(const char *name) {
    return name && name[0] != '\0' && strlen(name) < NAME_MAX_LENGTH;
}

static Group *find(const char *name) {
    return groups ? hash_map_get(groups, name) : NULL;
}

static Group *create(const char *name) {
    if (!groups)
        groups = hash_map_new();
    Group *group = groups ? calloc(1, sizeof(Group)) : NULL;
    if (!group)
        return NULL;
    group->name = hash_map_put(groups, name, group);
    if (!group->name) {
        free(group);
        return NULL;
    }
    return group;
}

static void drop_group(Group *group) {
    // The key is only freed once the entry is found
    hash_map_remove(groups, group->name);
    free(group->members);
    free(group);
}

static bool add_member(Group *group, Pid pid) {
    if (group->count == group->capacity) {
        size_t capacity = group->capacity ? group->capacity * 2 : 4;
        Pid *grown = realloc(group->members, capacity * sizeof(Pid));
        if (!grown)
            return false;
        group->members = grown;
        group->capacity = capacity;
    }
    group->members[group->count++] = pid;
    return true;
}

// Removes pid from the group, and the group once empty; returns false if pid is not in it
static bool remove_member(Group *group, Pid pid) {
    for (size_t i = 0; i < group->count; ++i) {
        if (group->members[i] != pid)
            continue;
        memmove(&group->members[i], &group->members[i + 1], (group->count - i - 1) * sizeof(Pid));
        if (--group->count == 0)
            drop_group(group);
        return true;
    }
    return false;
}

static bool contains(const Group *group, Pid pid) {
    for (size_t i = 0; i < group->count; ++i) {
        if (group->members[i] == pid)
            return true;
    }
    return false;
}

int clpf_group_join(Process *self, const char *name) {
    if (!valid_name(name))
        return -1;

    int result = -1;
    pthread_mutex_lock(&lock);
    Group *group = find(name);
    if (group && contains(group, self->pid)) {
        result = 0;
    } else {
        if (!group)
            group = create(name);
        if (group && add_member(group, self->pid)) {
            ++self->group_count;
            result = 0;
        } else if (group && group->count == 0) {
            drop_group(group); // A group never stays empty
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

int clpf_group_leave(Process *self, const char *name) {
    if (!valid_name(name))
        return -1;

    int result = -1;
    pthread_mutex_lock(&lock);
    Group *group = find(name);
    if (group && remove_member(group, self->pid)) {
        --self->group_count;
        result = 0;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

size_t clpf_group_members(const char *name, Pid *buffer, size_t max) {
    if (!valid_name(name))
        return 0;

    pthread_mutex_lock(&lock);
    Group *group = find(name);
    size_t count = group ? group->count : 0;
    if (buffer && count > 0)
        memcpy(buffer, group->members, (count < max ? count : max) * sizeof(Pid));
    pthread_mutex_unlock(&lock);
    return count;
}

size_t clpf_group_send(Process *self, const char *name, void *data, size_t size, Tag tag) {
    if (!valid_name(name))
        return 0;

    // Sending takes the receivers' locks: copy the members and send outside the group lock
    pthread_mutex_lock(&lock);
    Group *group = find(name);
    size_t count = group ? group->count : 0;
    Pid *members = count > 0 ? malloc(count * sizeof(Pid)) : NULL;
    if (members)
        memcpy(members, group->members, count * sizeof(Pid));
    pthread_mutex_unlock(&lock);
    if (!members)
        return 0;

    size_t delivered = 0;
    for (size_t i = 0; i < count; ++i) {
        if (clpf_send(self, members[i], data, size, tag) == 0)
            ++delivered;
    }
    free(members);
    return delivered;
}

typedef struct {
    Pid pid;
    Group *found; // A group pid is in, or NULL
} Membership;

static void find_membership(const char *name, void *value, void *arg) {
    (void)name;
    Membership *membership = arg;
    if (!membership->found && contains(value, membership->pid))
        membership->found = value;
}

void groups_forget(Pid pid) {
    pthread_mutex_lock(&lock);
    // Leaving may remove a group, which visiting cannot allow: one group per pass.
    // A process is in few groups (see Process.group_count).
    while (groups) {
        Membership membership = {.pid = pid, .found = NULL};
        hash_map_for_each(groups, find_membership, &membership);
        if (!membership.found)
            break;
        remove_member(membership.found, pid);
    }
    pthread_mutex_unlock(&lock);
}
