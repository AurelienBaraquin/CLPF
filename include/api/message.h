#pragma once

#include <api/memory.h>

#define INCLUDE_CLPF_MESSAGE

bool MessageMatchFn_SameTag(Message *msg, Tag tag);
bool MessageMatchFn_StringPrefix(Message *msg, Tag tag);
bool MessageMatchFn_IntGTE(Message *msg, Tag tag);
bool MessageMatchFn_IntLTE(Message *msg, Tag tag);
bool MessageMatchFn_IntInRange(Message *msg, Tag tag);
bool MessageMatchFn_ByKind(Message *msg, Tag tag);

#define TM_SAME \
    MessageMatchFn_SameTag
#define TM_PREFIX \
    MessageMatchFn_StringPrefix
#define TM_GTE \
    MessageMatchFn_IntGTE
#define TM_LTE \
    MessageMatchFn_IntLTE
#define TM_IN_RANGE \
    MessageMatchFn_IntInRange
#define TM_KIND \
    MessageMatchFn_ByKind

#define RECEIVE(tag) \
    receive(self, tag)

#define RECEIVE_RAW(tag) \
    __receive_internal(self, tag);

#define FETCH(tag) \
    fetch(self, tag)

#define SEND(pid, data, size, tag) \
    send(self, pid, data, size, tag)

#define REQUEST(pid, data, size, tag) \
    request(self, pid, data, size, tag)

// ONLY STATIC STRING ARE ALLOWED DO NOT PUT DYNAMIC STRING !!!!!
#define NOTIFY(pid, data) \
    send(self, pid, NULL, 0, MAKE_STR_TAG("notify::"data))

#define WAIT_NOTIF(data) \
    receive(self, MAKE_STR_TAG("notify::"data))
