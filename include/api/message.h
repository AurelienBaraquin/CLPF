#pragma once

#include <api/memory.h>

// Matchers for RECEIVE_IF / FETCH_IF: each tells whether a message matches a pattern tag
bool clpf_match_same(const Message *msg, Tag pattern);     // equal tags; NO_TAG matches any message
bool clpf_match_prefix(const Message *msg, Tag pattern);   // string tag starting with the pattern string
bool clpf_match_gte(const Message *msg, Tag pattern);      // integer tag >= pattern
bool clpf_match_lte(const Message *msg, Tag pattern);      // integer tag <= pattern
bool clpf_match_in_range(const Message *msg, Tag pattern); // integer tag within MAKE_RANGE_TAG(min, max)
bool clpf_match_kind(const Message *msg, Tag pattern);     // same TagKind (use MAKE_TAG_KIND)

#define TM_SAME     clpf_match_same
#define TM_PREFIX   clpf_match_prefix
#define TM_GTE      clpf_match_gte
#define TM_LTE      clpf_match_lte
#define TM_IN_RANGE clpf_match_in_range
#define TM_KIND     clpf_match_kind

// Blocks until the oldest message with this tag (NO_TAG: any message) arrives.
// The message belongs to the caller: release it with MSG_FREE.
#define RECEIVE(tag) clpf_receive(self, TM_SAME, tag)

// Blocks until the oldest message accepted by match arrives
#define RECEIVE_IF(match, pattern) clpf_receive(self, match, pattern)

// Like RECEIVE without blocking: NULL only when no message matches
#define FETCH(tag) clpf_fetch(self, TM_SAME, tag)

#define FETCH_IF(match, pattern) clpf_fetch(self, match, pattern)

// Copies size bytes of data into the receiver's heap: pointers inside the
// payload are meaningless to the receiver. Returns 0, or -1 if pid is gone.
#define SEND(pid, data, size, tag) clpf_send(self, pid, data, size, tag)

// Sends, then waits for the reply carrying the same tag (NULL if pid is gone)
#define REQUEST(pid, data, size, tag) clpf_request(self, pid, data, size, tag)

#define MSG_IS(msg, tag) clpf_match_same(msg, tag)

#define MSG_FREE(msg) clpf_msg_free(self, msg)

// Empty signal-like messages identified by a name (any string, built at run time or not)
#define NOTIFY(pid, name) SEND(pid, NULL, 0, MAKE_NOTIFY_TAG(name))

#define WAIT_NOTIF(name) MSG_FREE(RECEIVE(MAKE_NOTIFY_TAG(name)))
