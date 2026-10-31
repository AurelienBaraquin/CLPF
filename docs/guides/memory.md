# Memory

Header: `api/memory.h`

Each process has a **private heap**, a [TLSF](http://www.gii.upv.es/tlsf/)
allocator on memory mapped for that process alone. It grows on demand and is
released all at once when the process ends. A new heap may start on memory an
ended process used: like `malloc`, `ALLOC` does not clear what it returns.

| Macro | Meaning |
|---|---|
| `ALLOC(size)` | Allocates in the process heap; `NULL` if out of memory |
| `FREE(ptr)` | Releases it (`FREE(NULL)` does nothing) |

## Who owns what

| Memory | Owner | Released by |
|---|---|---|
| Your locals, the task's arguments | The process stack | Returning |
| `ALLOC`, `FORMAT`, `GETLINE` results | The process heap | `FREE`, or the end of the process |
| A received `Message` | The receiver's heap | `MSG_FREE`, or the end of the process |
| `malloc` memory | The whole program | `free` only |

The end of a process releases everything in its heap. A process that ends
soon after allocating can skip `FREE`; a long-running one must free what it
no longer needs, or its heap keeps growing.

## Rules

- **Do not hand heap memory to another process.** Its pointer is only valid
  for the owner, and only while the owner lives. Send a copy.
- `malloc` works and is shared by every process. Use it for data that
  outlives a process or is shared on purpose, such as read-only tables, and
  then protect it yourself as in any threaded program.
- Running out of memory is reported, not fatal: `ALLOC` returns `NULL`, and
  `SEND` returns `-1` when the receiver's heap cannot hold the message.

## Stacks

Stacks are separate from the heap: 16 KiB per process by default. Avoid
large local arrays, or give the process a bigger stack (see
[processes](processes.md#stacks)).
