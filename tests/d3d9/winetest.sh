#!/bin/bash
# Runs one of Wine's d3d9 conformance suites against the DXMT runtime run.sh set up, and counts failures per test.
# Usage: tests/d3d9/winetest.sh <wine-build-dir> <wine-source-dir> <suite> [test_function ...|--list]
#   suite: visual, device, stateblock, d3d9ex; naming functions runs only those, --list prints them
set -uo pipefail

build="$(cd "${1:?wine build dir}" && pwd)"
source="$(cd "${2:?wine source dir}" && pwd)"
suite="${3:?suite}"
shift 3
work="$(pwd)/toolchains/test"
out="$work/logs/winetest-$suite.out"
exe="$build/dlls/d3d9/tests/x86_64-windows/d3d9_test.exe"
export WINEPREFIX="$work/pfx" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
filter="$(cd "$(dirname "$0")" && pwd)/winetest_filter.py"

"$(dirname "$0")/install.sh"

if [[ "${1:-}" == --list ]]; then
  python3 -I "$filter" "$source/dlls/d3d9/tests/$suite.c" /dev/null --list
  exit
fi

if (($#)); then
  # a copy of the suite that runs only the functions in WINETEST_ONLY, linked with Wine's other test objects
  one="$work/one"
  mkdir -p "$one"
  exe="$one/d3d9_test.exe"
  objs="$build/dlls/d3d9/tests/x86_64-windows"
  if [[ ! -f "$one/$suite.o" || "$source/dlls/d3d9/tests/$suite.c" -nt "$one/$suite.o" || "$filter" -nt "$one/$suite.o" ]]; then
    python3 -I "$filter" "$source/dlls/d3d9/tests/$suite.c" "$one/$suite.c" || exit 1
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
  export WINETEST_ONLY="$(IFS=,; echo "$*")"
  out="$work/logs/winetest-$suite-only.out"
fi

"$work/wine/bin/wine" "$exe" "$suite" > "$out" 2>&1 &
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
for line in open(sys.argv[2], errors='replace'):
    if m := re.search(r'\.c:(\d+): Test failed', line):
        failures[function(int(m.group(1)))] += 1
    elif 'tests executed' in line:
        print(line.split(':', 1)[-1].strip())
for name, count in failures.most_common():
    print(f'{count:5} {name}')
print(f'{sum(failures.values())} failures in {len(failures)} tests')
EOF
echo "log: $out"
