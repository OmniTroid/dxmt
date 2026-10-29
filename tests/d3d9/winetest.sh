#!/bin/bash
# Runs one of Wine's d3d9 conformance suites against the DXMT runtime run.sh set up, and counts failures per test.
# Usage: tests/d3d9/winetest.sh <wine-build-dir> <wine-source-dir> <suite> [test_function ...|--list]
#   suite: visual, device, stateblock, d3d9ex; naming functions runs only those, -name skips one, --list prints them
# Every test function runs in its own process: one that stalls (deadlocks, macOS display mode switches that never
# return) is killed after STALL seconds without output, its stacks kept, and the run goes on with the next.
set -uo pipefail

build="$(cd "${1:?wine build dir}" && pwd)"
source="$(cd "${2:?wine source dir}" && pwd)"
suite="${3:?suite}"
shift 3
work="$(pwd)/toolchains/test"
export WINEPREFIX="$work/pfx" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
filter="$(cd "$(dirname "$0")" && pwd)/winetest_filter.py"
suite_source="$source/dlls/d3d9/tests/$suite.c"

"$(dirname "$0")/install.sh"

all=($(python3 -I "$filter" "$suite_source" /dev/null --list))
if [[ "${1:-}" == --list ]]; then
  printf '%s\n' "${all[@]}"
  exit
fi

# a copy of the suite that runs only the function in WINETEST_ONLY, linked with Wine's other test objects
one="$work/one"
mkdir -p "$one"
exe="$one/d3d9_test-$suite.exe"
objs="$build/dlls/d3d9/tests/x86_64-windows"
if [[ ! -f "$one/$suite.o" || "$suite_source" -nt "$one/$suite.o" || "$filter" -nt "$one/$suite.o" ]]; then
  python3 -I "$filter" "$suite_source" "$one/$suite.c" || exit 1
  x86_64-w64-mingw32-gcc -c -o "$one/$suite.o" "$one/$suite.c" -I"$build/dlls/d3d9/tests" \
    -I"$source/dlls/d3d9/tests" -I"$build/include" -I"$source/include" -I"$source/include/msvcrt" \
    -D_MSVCR_VER=0 -D__WINESRC__ -D__WINE_PE_BUILD -fno-strict-aliasing -mcx16 -mcmodel=small -O2 -w || exit 1
  rm -f "$exe"
fi
if [[ ! -f "$exe" ]]; then
  linked=()
  for name in d3d9ex device stateblock visual testlist; do
    if [[ $name == "$suite" ]]; then linked+=("$one/$suite.o"); else linked+=("$objs/$name.o"); fi
  done
  (cd "$build" && tools/winegcc/winegcc -o "$exe" --wine-objdir . -b x86_64-w64-mingw32 "${linked[@]}" \
    dlls/d3d9/x86_64-windows/libd3d9.a dlls/user32/x86_64-windows/libuser32.a dlls/gdi32/x86_64-windows/libgdi32.a \
    dlls/winecrt0/x86_64-windows/libwinecrt0.a dlls/msvcrt/x86_64-windows/libmsvcrt.a \
    dlls/kernel32/x86_64-windows/libkernel32.a dlls/ntdll/x86_64-windows/libntdll.a) || exit 1
fi

tests=()
skip=" "
for arg in "$@"; do
  if [[ $arg == -* ]]; then skip+="${arg#-} "; else tests+=("$arg"); fi
done
((${#tests[@]})) || tests=("${all[@]}")
out="$work/logs/winetest-$suite.out"
(($#)) && out="$work/logs/winetest-$suite-only.out"
: > "$out"
rm -f "$out".*.stall.txt

stalled=()
for test in "${tests[@]}"; do
  [[ $skip == *" $test "* ]] && continue
  test_out="$out.$test"
  WINETEST_ONLY="$test" "$work/wine/bin/wine" "$exe" "$suite" > "$test_out" 2>&1 &
  pid=$!
  for ((i = 0; ; i++)); do
    kill -0 $pid 2>/dev/null || break
    if ((i >= 5 && $(date +%s) - $(stat -f %m "$test_out") > ${STALL:-60})); then
      sample $pid 1 -f "$out.$test.stall.txt" > /dev/null 2>&1
      "$work/wine/bin/wineserver" -k
      stalled+=("$test")
      echo "STALLED: $test after $(grep -v '^info' "$test_out" | tail -1 | cut -c1-100) (stacks: $out.$test.stall.txt)"
      break
    fi
    sleep 1
  done
  wait $pid 2>/dev/null
  # a process that dies without Wine's summary line crashed
  grep -q 'tests executed' "$test_out" || [[ " ${stalled[*]-} " == *" $test "* ]] || echo "CRASHED: $test"
  echo "### $test" >> "$out"
  cat "$test_out" >> "$out"
  rm -f "$test_out"
done

python3 -I - "$suite_source" "$out" ${stalled[@]+"${stalled[@]}"} <<'EOF'
import collections, re, sys
text = open(sys.argv[1]).read()
tests = set(re.findall(r'^\s+(\w+)\(\);$', text[text.index('START_TEST('):], re.M))
functions = [(i, m.group(1)) for i, line in enumerate(text.split('\n'), 1)
             for m in [re.match(r'^static void (\w+)\(', line)] if m and m.group(1) in tests]
def function(line):
    name = '?'
    for start, candidate in functions:
        if start > line:
            break
        name = candidate
    return name
failures = collections.Counter()
# Wine counts checks it marks todo_wine as failures when they pass; those are places DXMT is right and Wine isn't
better = executed = 0
crashed = []
current = None
for line in open(sys.argv[2], errors='replace'):
    if line.startswith('### '):
        current = line[4:].strip()
    elif m := re.search(r'\.c:(\d+): Test failed', line):
        failures[function(int(m.group(1)))] += 1
    elif 'Test succeeded inside todo block' in line:
        better += 1
    elif m := re.search(r'(\d+) tests executed', line):
        executed += int(m.group(1))
for name, count in failures.most_common():
    print(f'{count:5} {name}')
print(f'{executed} checks, {sum(failures.values())} failures in {len(failures)} tests, {better} todo_wine checks passing')
if sys.argv[3:]:
    print('stalled: ' + ' '.join(sys.argv[3:]))
EOF
echo "log: $out"
