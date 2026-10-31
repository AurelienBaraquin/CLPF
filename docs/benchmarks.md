# Benchmarks

`bench/clpf_bench.c` measures the costs that matter when designing with
processes. Run it from a Release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target bench
```

`clpf_bench --quick` does 100 times less work. ctest runs it to keep the
benchmark working, not to measure anything.

## Results

On a 6-core x86-64 machine (Linux under WSL2, so 4 scheduler threads), in
Release:

| Benchmark | Result | What it does |
|---|---|---|
| spawn + exit | 255 000 processes/s | Spawns 100 000 processes that send one message and end |
| parallel spawn + exit (8) | 1 million processes/s | 8 processes spawn 12 500 such processes each, at once |
| one-way messages | 2.3 million messages/s | One process sends 1 000 000 small messages to another |
| ping-pong round trip | 1.5 µs | Two processes exchange 200 000 request/answer pairs |
| request, 1000 messages queued | 1.6 µs | `REQUEST_REF` to a server while 1000 unrelated messages wait in the mailbox |
| spawn of waiting processes | 42 µs each | Spawns 10 000 processes that wait in `RECEIVE` |
| memory per waiting process | 16 KiB | Resident memory of those 10 000 processes, stack and heap |
| wake and collect all | 18 ms | Wakes the 10 000 with a message each, and collects their answers |
| parallel ping-pong (8 pairs) | 2.7 million round trips/s | 8 ping-pong pairs at once, spread over the schedulers |
| idle program, resident memory | 2.2 MB | A program whose entry does nothing |

Your numbers will differ: run the benchmark on your machine.

### What the internal work changed

| Measure | Before | After | Change |
|---|---|---|---|
| Idle program, resident memory | 34 MB | 2.2 MB | The process table is only touched where slots are used |
| Request with 1000 messages queued | 9 µs | 1.6 µs | `REQUEST_REF` skips the messages older than the request |
| 20 000 `RECEIVE_TIMEOUT` with 50 000 timers pending | 5.4 s | 0.3 s | Timers per scheduler, cancelled in O(1) |
| Spawn + exit | 230 000/s | 255 000/s | Processes end on their scheduler, memory is unmapped by the reaper |
| Parallel spawn + exit (8) | 780 000/s | 1 million/s | No table lock to spawn or end a process |
| Spawning 1000 waiting processes | 45 µs each | 17 µs each | With `CLPF_POOL_PREWARM=1024` |
| A process waiting for its next message | 17 KiB | 12 KiB | With `HIBERNATE`: no stack, unused heap given back |
| Registered name of 63 characters | up to 33 KiB | about 100 bytes | The registry is a hash map instead of a trie |
| `ALLOC` or message larger than the free heap | failed | works | The heap grows by enough for TLSF's headers and size classes |

## Reading them

- **Messages are cheap.** A message costs about the same as a function call
  that takes a lock. Ping-pong pairs scale with the schedulers, as pairs run
  in parallel.
- **Short-lived processes are cheap.** A process that ends gives its stack
  and its initial heap pool back to a pool, and the next process starts on
  them without any system call: spawning and ending costs about 4 µs. Each
  pool keeps up to 1024 blocks (`-DCLPF_BLOCK_POOL_CAPACITY=...`); stacks
  of a non-default `stack_size` are always mapped and unmapped.
- **Many children of one process cost nothing extra.** Each process holds
  its own place in its parent's list of children: a spawn or an exit costs
  the same next to 10 000 live siblings as alone. A process that stays alive
  still costs about 40 µs to spawn once the pools are empty, as its stack
  and heap are mapped (two `mmap` and an `mprotect`).
  `CLPF_POOL_PREWARM=N` maps up to 1024 of them at startup: the first ones
  then spawn in about 17 µs.
