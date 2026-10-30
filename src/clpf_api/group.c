#include <api/group.h>
#include <groups.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[NAME_MAX_LENGTH];
    Pid *members; // In join order
    size_t count;
    size_t capacity;
} Group;

// Groups are few and small: arrays searched under one lock are enough
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Group **groups; // Guarded by lock, as below; empty groups are freed
static size_t group_count;
static size_t group_capacity;

static bool valid_name(const char *name) {
    return name && name[0] != '\0' && strlen(name) < NAME_MAX_LENGTH;
}

static Group *find(const char *name, size_t *index) {
    for (size_t i = 0; i < group_count; ++i) {
        if (strcmp(groups[i]->name, name) == 0) {
            if (index)
                *index = i;
            return groups[i];
        }
    }
    return NULL;
}

static Group *create(const char *name) {
    if (group_count == group_capacity) {
        size_t capacity = group_capacity ? group_capacity * 2 : 8;
        Group **grown = (Group **)realloc((void *)groups, capacity * sizeof(Group *));
        if (!grown)
            return NULL;
        groups = grown;
        group_capacity = capacity;
    }
    Group *group = calloc(1, sizeof(Group));
    if (!group)
        return NULL;
    memcpy(group->name, name, strlen(name) + 1); // length checked by valid_name
    groups[group_count++] = group;
    return group;
}

// The last group takes its place
static void drop_group(size_t index) {
    free(groups[index]->members);
    free(groups[index]);
    groups[index] = groups[--group_count];
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

// Removes pid from the group at index, and the group once empty; returns false if pid is not in it
static bool remove_member(size_t index, Pid pid) {
    Group *group = groups[index];
    for (size_t i = 0; i < group->count; ++i) {
        if (group->members[i] != pid)
            continue;
        memmove(&group->members[i], &group->members[i + 1], (group->count - i - 1) * sizeof(Pid));
        if (--group->count == 0)
            drop_group(index);
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
    size_t index = 0;
    Group *group = find(name, &index);
    if (group && contains(group, self->pid)) {
        result = 0;
    } else {
        if (!group) {
            group = create(name);
            index = group_count - 1;
        }
        if (group && add_member(group, self->pid)) {
            ++self->group_count;
            result = 0;
        } else if (group && group->count == 0) {
            drop_group(index); // A group never stays empty
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
    size_t index = 0;
    if (find(name, &index) && remove_member(index, self->pid)) {
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
    Group *group = find(name, NULL);
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
    Group *group = find(name, NULL);
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

void groups_forget(Pid pid) {
    pthread_mutex_lock(&lock);
    // remove_member may move the last group to index i: look at i again then
    for (size_t i = 0; i < group_count;) {
        size_t before = group_count;
        if (!remove_member(i, pid) || group_count == before)
            ++i;
    }
    pthread_mutex_unlock(&lock);
}
