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
| spawn + exit | 230 000 processes/s | Spawns 100 000 processes that send one message and end |
| one-way messages | 2.0 million messages/s | One process sends 1 000 000 small messages to another |
| ping-pong round trip | 1.6 µs | Two processes exchange 200 000 request/answer pairs |
| spawn of waiting processes | 49 µs each | Spawns 10 000 processes that wait in `RECEIVE` |
| memory per waiting process | 15 KiB | Resident memory of those 10 000 processes, stack and heap |
| wake and collect all | 16 ms | Wakes the 10 000 with a message each, and collects their answers |
| parallel ping-pong (8 pairs) | 2.3 million round trips/s | 8 ping-pong pairs at once, spread over the schedulers |

Your numbers will differ: run the benchmark on your machine.

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
  still costs about 50 µs to spawn once the pools are empty, as its stack
  and heap are mapped (two `mmap` and an `mprotect`).
