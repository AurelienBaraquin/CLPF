#pragma once

#include <clpf_internal.h>

#define INCLUDE_CLPF_MESSAGE

bool MessageMatchFn_SameTag(Message *msg, Tag userdata);
bool MessageMatchFn_StringPrefix(Message *msg, Tag userdata);
bool MessageMatchFn_IntGTE(Message *msg, Tag userdata);
bool MessageMatchFn_IntLTE(Message *msg, Tag userdata);
bool MessageMatchFn_IntInRange(Message *msg, Tag userdata);
bool MessageMatchFn_ByKind(Message *msg, Tag userdata);

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

#define RECEIVE(userdata) \
    receive(self, userdata)

#define SEND(pid, data, size) \
    send(self, pid, data, size, NO_TAG)

#define SEND_TAG(pid, data, size, tag) \
    send(self, pid, data, size, tag)


// ONLY STATIC STRING ARE ALLOWED DO NOT PUT DYNAMIC STRING !!!!!
#define NOTIFY(pid, data) \
    send(self, pid, NULL, 0, MAKE_STR_TAG("notify::"data))

#define WAIT_NOTIF(data) \
    receive(self, MAKE_STR_TAG("notify::"data))
