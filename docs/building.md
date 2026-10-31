# Building and contributing

## Building

```sh
git clone --recursive https://github.com/AurelienBaraquin/CLPF.git
cd CLPF
cmake -S . -B build
cmake --build build -j
```

| CMake option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Debug` | `Release` for `-O2` and no assertions |
| `CLPF_SANITIZE` | empty | `address` or `thread`: builds everything, tests included, with that sanitizer |
| `CLPF_TRACE` | `OFF` | Logs every process creation and destruction to stdout |
| `CLPF_BUILD_TESTS` | `ON` | Builds the tests |
| `CLPF_BUILD_EXAMPLES` | `ON` | Builds `examples/` |

To change the default process stack, add `-DCLPF_DEFAULT_STACK_SIZE=65536`
to `CMAKE_C_FLAGS`.

The build produces:

- `libclpf.a` and its vendored dependencies, `libclpf_aco.a`, `libclpf_tlsf.a`
  and `libclpf_stb_sprintf.a`;
- the showcase, `clpf_demo` (`main.c`);
- the tests and the examples.

### Installing

```sh
cmake --install build --prefix /usr/local     # or any prefix
```

This installs the libraries, the headers under `include/clpf`, a CMake package
(`find_package(clpf)` gives the targets `clpf::clpf` and
[`clpf::auto_reduce`](guides/scheduling.md#automatic-reductions)) and a relocatable
`clpf.pc` for pkg-config. See [getting started](getting-started.md).

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Each test in `tests/` is a small CLPF program that reports with
`TEST_ASSERT` / `TEST_PASS` (`tests/test_helpers.h`); a hang fails through the
CTest timeout. A few run under `taskset -c 0`, to check behaviour on a single
scheduler. The examples are run as tests too.

With sanitizers:

```sh
cmake -S . -B build-thread -DCLPF_SANITIZE=thread
cmake --build build-thread -j
setarch -R ctest --test-dir build-thread
```

ThreadSanitizer needs address space layout randomisation off (`setarch -R`)
on recent kernels, or `sudo sysctl vm.mmap_rnd_bits=28`. Under a sanitizer,
tests define `CLPF_SANITIZED` and scale down their largest scenarios.

## Code style and static analysis

```sh
pip install clang-format==23.1.3 clang-tidy==23.1.4
git ls-files 'src/*.[ch]' 'include/*.h' 'tests/*.[ch]' 'examples/*.c' main.c | xargs clang-format -i
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
git ls-files 'src/*.c' 'tests/*.c' 'examples/*.c' main.c | xargs clang-tidy -p build --quiet
```

The configuration is in `.clang-format` and `.clang-tidy`. Vendored code in
`external/` is not formatted or checked.

## Documentation

The documentation is a [MkDocs](https://www.mkdocs.org/) site in `docs/`,
with the Material theme:

```sh
pip install mkdocs-material
mkdocs serve        # live preview on http://127.0.0.1:8000
mkdocs build --strict
```

Every complete program in the documentation (a C block containing
`CLPF_ENTRY`) is compiled and run by `scripts/check_doc_examples.py`. It must
exit with 0, or with the code given by an `<!-- expect-exit: N -->` line just
before the block:

```sh
python3 scripts/check_doc_examples.py build README.md docs/*.md docs/guides/*.md
```

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request and every push to
`main`:

- tests in Debug and Release;
- tests under AddressSanitizer and ThreadSanitizer;
- an example built against the installed package;
- formatting and clang-tidy.

`.github/workflows/docs.yml` builds the site, checks the examples in the
documentation, and publishes the site to GitHub Pages from `main`.

## Contributing

- One logical change per commit, with a [conventional](https://www.conventionalcommits.org/)
  message (`fix:`, `feat:`, `perf:`, `docs:`...).
- Add a test that fails without your change.
- Run the tests in a normal build and under both sanitizers before opening a
  pull request; the CI checks the rest.

## Repository layout

| Path | Content |
|---|---|
| `include/api/` | Public API |
| `include/`, `src/` | Runtime |
| `src/native_task/` | System processes: launcher, I/O server, supervisor |
| `external/` | Vendored libaco (submodule), TLSF, stb_sprintf |
| `tests/` | Test programs |
| `examples/` | Short runnable programs |
| `main.c` | The showcase (`clpf_demo`) |
| `docs/`, `mkdocs.yml` | This documentation |
| `cmake/` | Package config templates |
