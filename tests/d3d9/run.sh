#!/bin/bash
# Runs the d3d9 tests (64- and 32-bit) in a fresh Wine runtime with this tree's DXMT DLLs installed.
# Usage: tests/d3d9/run.sh <wine-install-prefix>   (from the repository root, after building build/ and build32/)
set -uo pipefail

wine_install="$(cd "${1:?wine install prefix}" && pwd)"
root="$(pwd)"
work="$root/toolchains/test"
runtime="$work/wine"

rm -rf "$runtime" "$work/pfx"
mkdir -p "$work/logs"
cp -Rc "$wine_install" "$runtime"

for arch in x86_64 i386; do
  build="$root/build"
  [ $arch = i386 ] && build="$root/build32"
  for dll in d3d9/d3d9 d3d11/d3d11 d3d10/d3d10core dxgi/dxgi winemetal/winemetal; do
    cp "$build/src/$dll.dll" "$runtime/lib/wine/$arch-windows/"
  done
done
cp "$root/build/src/winemetal/unix/winemetal.so" "$runtime/lib/wine/x86_64-unix/"

export WINEPREFIX="$work/pfx" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" DXMT_LOG_PATH="$work/logs"
"$runtime/bin/wine" wineboot -i > /dev/null 2>&1
# a crash dialog would block the run forever
"$runtime/bin/wine" reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1

failed=0
for build in build build32; do
  for exe in "$root/$build"/tests/d3d9/d3d9_*.exe; do
    name="$(basename "$exe" .exe)"
    [ "$name" = d3d9_clear ] && continue
    result="$("$runtime/bin/wine" "$exe" 2>&1 | grep -E '^(FAIL|[0-9]+ failure)')"
    echo "$build/$name: ${result:-no output}"
    echo "$result" | grep -q '^0 failure' || failed=1
  done
done
"$runtime/bin/wineserver" -k
exit $failed
