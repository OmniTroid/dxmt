#!/bin/bash
# Runs one of Wine's d3d9 conformance suites against the DXMT runtime run.sh set up, and counts failures per test.
# Usage: tests/d3d9/winetest.sh <wine-build-dir> <wine-source-dir> <suite>   (e.g. visual, device, stateblock, d3d9ex)
set -uo pipefail

build="${1:?wine build dir}"
source="${2:?wine source dir}"
suite="${3:?suite}"
work="$(pwd)/toolchains/test"
out="$work/logs/winetest-$suite.out"
export WINEPREFIX="$work/pfx" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="

"$work/wine/bin/wine" "$build/dlls/d3d9/tests/x86_64-windows/d3d9_test.exe" "$suite" > "$out" 2>&1 &
pid=$!
for ((i = 0; i < ${TIMEOUT:-1800}; i++)); do
  kill -0 $pid 2>/dev/null || break
  sleep 1
done
if kill -0 $pid 2>/dev/null; then
  "$work/wine/bin/wineserver" -k
  echo "TIMEOUT"
fi

python3 -I - "$source/dlls/d3d9/tests/$suite.c" "$out" <<'EOF'
import collections, re, sys
functions = [(i, m.group(1)) for i, line in enumerate(open(sys.argv[1]), 1)
             for m in [re.match(r'^static void (\w+)\(', line)] if m]
def function(line):
    name = '?'
    for start, candidate in functions:
        if start > line:
            break
        name = candidate
    return name
failures = collections.Counter()
for line in open(sys.argv[2], errors='replace'):
    if m := re.search(r'\.c:(\d+): Test failed', line):
        failures[function(int(m.group(1)))] += 1
    elif 'tests executed' in line:
        print(line.split(':', 1)[-1].strip())
for name, count in failures.most_common():
    print(f'{count:5} {name}')
print(f'{sum(failures.values())} failures in {len(failures)} tests')
EOF
