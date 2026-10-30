#!/usr/bin/env python3
"""Compile and run every complete C program shown in the documentation.

A ```c block containing CLPF_ENTRY is a complete program: it is built against
the library in the given build directory and must exit with 0, or with the
code given by an "<!-- expect-exit: N -->" line just before the block.

Usage: scripts/check_doc_examples.py BUILD_DIR FILE.md...
"""
import os
import re
import subprocess
import sys
import tempfile

BLOCK = re.compile(r'(?:<!-- expect-exit: (\d+) -->\s*\n)?```c\n(.*?)```', re.S)


def main():
    build_dir, files = sys.argv[1], sys.argv[2:]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror',
             '-I' + os.path.join(root, 'include'), '-I' + os.path.join(root, 'external')]
    libs = ['-L' + build_dir, '-lclpf', '-lclpf_tlsf', '-lclpf_aco', '-lclpf_stb_sprintf', '-lpthread']

    checked, failures = 0, 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in files:
            text = open(path, encoding='utf-8').read()
            for number, match in enumerate(BLOCK.finditer(text)):
                expected, code = int(match.group(1) or 0), match.group(2)
                if 'CLPF_ENTRY' not in code:
                    continue
                checked += 1
                line = text.count('\n', 0, match.start()) + 1
                where = f'{path}:{line}'
                source = os.path.join(tmp, f'example_{checked}.c')
                binary = source[:-2]
                with open(source, 'w') as out:
                    out.write(code)

                build = subprocess.run(['gcc', *flags, source, *libs, '-o', binary], capture_output=True, text=True)
                if build.returncode != 0:
                    print(f'{where}: does not compile\n{build.stderr}')
                    failures += 1
                    continue
                try:
                    run = subprocess.run([binary], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=20)
                except subprocess.TimeoutExpired:
                    print(f'{where}: still running after 20 s')
                    failures += 1
                    continue
                if run.returncode != expected:
                    print(f'{where}: exit code {run.returncode}, expected {expected}\n{run.stdout}{run.stderr}')
                    failures += 1

    print(f'{checked} examples checked, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
