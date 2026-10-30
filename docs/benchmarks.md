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
| spawn + exit | 16 000 processes/s | Spawns 100 000 processes that send one message and end |
| one-way messages | 2.2 million messages/s | One process sends 1 000 000 small messages to another |
| ping-pong round trip | 1.4 µs | Two processes exchange 200 000 request/answer pairs |
| spawn of waiting processes | 60 µs each | Spawns 10 000 processes that wait in `RECEIVE` |
| memory per waiting process | 16 KiB | Resident memory of those 10 000 processes, stack and heap |
| wake and collect all | 510 ms | Wakes the 10 000 with a message each, and collects their answers |
| parallel ping-pong (8 pairs) | 2.8 million round trips/s | 8 ping-pong pairs at once, spread over the schedulers |

Your numbers will differ: run the benchmark on your machine.

## Reading them

- **Messages are cheap.** A message costs about the same as a function call
  that takes a lock. Ping-pong pairs scale with the schedulers, as pairs run
  in parallel.
- **Processes are cheap in memory, not in creation time.** Each process maps
  its own stack and heap, and unmaps them when it ends. Those system calls
  dominate spawning and ending. Keep long-lived processes and send them
  work, instead of one process per tiny task.
- **Mass exits are slow.** Unmapping memory makes the kernel flush the TLB
  of every core: 10 000 processes ending at once slow everyone down for a
  while. Pooling stacks and heaps would remove that cost; it is listed in
  the project's todo.
