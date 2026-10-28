# Testing the D3D9 frontend

How to build and test `src/d3d9` and airconv's D3D9 front end, and the traps found so far. Add to this whenever something costs time.

## Setup

Wine comes from the CrossOver 26.3 fork at [OmniTroid/wine](https://github.com/OmniTroid/wine), built into `toolchains/wine-cx` (gitignored):

```sh
~/code/wine/build-macos.sh toolchains/wine-cx/build toolchains/wine-cx/install
meson setup --cross-file build-win64.txt -Dnative_llvm_path=toolchains/llvm -Dwine_install_path=toolchains/wine-cx/install -Denable_d3d9=true -Denable_tests=true build
meson setup --cross-file build-win32.txt -Dwine_install_path=toolchains/wine-cx/install -Denable_d3d9=true -Denable_tests=true build32
```

LLVM 15 (x86_64) is built into `toolchains/llvm` as described in [DEVELOPMENT.md](DEVELOPMENT.md).

## Running

```sh
tests/d3d9/run.sh toolchains/wine-cx/install
```

Copies the Wine install to `toolchains/test/wine`, installs this tree's DLLs for both architectures, creates a fresh prefix and runs every `tests/d3d9` test. Each test prints `PASS`/`FAIL` lines and exits with its failure count.

The airconv tests in `tests/airconv_d3d9` render one pixel per case on the GPU and compare it with a Python reference; see the README there.

## Rules

- Keep anything reused across sessions in the repo or `toolchains/`. A scratchpad under `/private/tmp` was wiped overnight and took the test runtime, tools and corpus with it.
- Prove a new check can fail: break the feature on purpose (e.g. remove the half-pixel viewport offset) and confirm the check reports `FAIL`. Several "passing" checks turned out to test nothing. Break one thing at a time: one broad breakage (e.g. a bad pretransformed `w`) fails every check and hides whether the others are covered.
- When a test disagrees with the expectation, re-derive the expectation before touching the code. Wrong expectations so far: SRCALPHA/INVSRCALPHA also blends alpha (0.75, not 1.0); a 2x2 texture sampled at uv (0.25, 0.5) is texel (0, 1); a constant with a negative z makes `texkill` discard.
- Screenshots: capture a single window by its exact title (`screencapture -l <id>`), never the whole screen, and never pick a window by owner process: other Wine apps (games) may be running.

## Traps

**Wine runtime**

- A prefix keeps its own copies of builtin DLLs in `drive_c/windows/system32` and Wine loads those. Copying a new `d3d9.dll` into the Wine install after the prefix exists changes nothing. Replace the `system32` copy too, or recreate the prefix (`run.sh` does).
- First prefix creation shows a Mono/Gecko install dialog and blocks forever headless. Set `WINEDLLOVERRIDES="mscoree,mshtml="`.
- A newly added builtin DLL (like `winemetal.dll`) is only found once `wineboot -u` or a new prefix registers it.
- `wineserver -k` only stops the server for the current `WINEPREFIX`; set it explicitly so other Wine sessions are untouched.
- Test executables need `-static` or they fail on missing `libstdc++-6.dll`/`libgcc_s_*.dll` (exit code 53).

**Building Wine on macOS**

- When the Xcode SDK is newer than the running macOS, new APIs are weakly linked and NULL at runtime. `pipe2` (macOS 27 SDK, macOS 26 host) crashed every process in `init_thread_pipe`; fixed in the fork. After a build, check `nm -m` on `lib/wine/x86_64-unix/*.so` for unexpected `weak external` symbols.
- CrossOver's `win32u` assumed MoltenVK; fixed in the fork. Don't patch around it with `CPPFLAGS=-D...=\"...\"`: the quoting breaks every configure compile test and silently disables headers.
- Homebrew only has arm64 libraries, so x86_64 Wine is built without freetype, gnutls, gstreamer and friends.
- Don't build against another project's Wine tree; Neutron's was rebuilt mid-session and the build broke.

**airconv**

- The cbuffer table is Metal buffer 0 and the argument table buffer 1. `ConstanttBufferTableBindIndex`/`ArgumentBufferBindIndex` (29/30) in the reflection are not the bind points.
- D3D9 `def` constants are read with swizzles and negation, which DXBC immediates never have; the immediate loader must apply both.
- New Wine unix calls are indexed by position in `__wine_unix_call_funcs`; count entries by commas, not lines (`MTLTexture_getBytes` hid at 145).
- New compilation argument types need a case in `sm50_compilation_argument32_convert` or 32-bit apps silently drop them.

**Writing test shaders with Wine's d3dcompiler (vkd3d)**

- It ignores `register(bN)` and puts HLSL bools in a float constant; bool registers, `loop aL`, `mova`, predication, subroutines and `texkill` are only covered by the hand-assembled `sm3_raw_tests.py`.
- It rejects loops bounded by an int uniform and the `FOG` semantic in some targets. CrossOver 26.3's vkd3d rejects `[loop]` in SM2/SM3 entirely (E5017 "not yet implemented"), so the HLSL loop cases skip.
- Each Wine process costs seconds to start; compile many shaders with one `hlslc.exe -b jobs.txt` call, as `sm3_tests.py` does.
- `D3DDisassemble` works on SM1-3 bytecode: `hlslc.exe -d file.bin`.

**Shell**

- In zsh, `${PIPESTATUS[0]}` is empty; check `$?` without a pipe.
- `cmd1 && python3 - <<EOF` skips the script if `cmd1` fails, e.g. a `grep` that finds nothing.
- In loops that `cd`, use absolute paths for binaries.
