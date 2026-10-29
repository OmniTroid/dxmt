# D3D9 on DXMT: plan

A `src/d3d9` frontend (d3d9.dll) that sits on DXMT's existing core the way `src/d3d11` does, plus an SM1-3 decoder in airconv.

## Why

There is no Metal-native D3D9 path on macOS (project-eve `graphics.md`, "Implications"): wined3d tops out low on GL 4.1 and on Vulkan-on-Metal, DXVK can't run on upstream MoltenVK or current KosmicKrisp (no `geometryShader`), and DXVK-macOS is frozen at 1.10.3. D3DMetal has no D3D9.

## Decisions to make first

1. **Upstream or fork.** Decided: hard fork. Upstream forbids AI-written code, so this work stays here and is not sent upstream.
2. **32-bit first.** Most D3D9 games are 32-bit. `build-win32.txt` works but still needs the 64-bit unixlib, so confirm the WoW64 path end to end in phase 1, not later.
3. **Scope of SM1.x.** ps_1_x has a long tail (`texm3x*`, `texbem`, modifiers). Ship vs/ps 2.0 and 3.0 first; add 1.x after.
4. **D3D8.** Later, as a thin layer onto d3d9 (the d8vk approach), not part of this plan.

## Architecture

| Layer | Reuse | New |
|---|---|---|
| Core | `dxmt::Device`, `CommandQueue`, `ArgumentEncodingContext`, `Buffer`, `Texture`, `DynamicBuffer`, `StagingResource`, `Sampler`, `ResourceInitializer`, occlusion queries | none |
| Presentation | `Presenter`, `WMT::CreateMetalViewFromHWND`, `wsi::*` (none need DXGI) | d3d9 swapchain, `Reset`, gamma via `DXMTGammaRamp` |
| Adapter | `WMT::CopyAllDevices` + discrete-GPU pick copied from `dxgi_factory.cpp:178` | `IDirect3D9Ex`, caps, mode enumeration |
| Formats | `MTLQueryDXGIFormat` cases, D24 emulation | `D3DFORMAT` mapper; swizzle pseudo-formats in `winemetal.h` for L8, A8L8, A4L4, X4R4G4B4, X1R5G5B5 |
| Shaders | airconv IR, VS/PS drivers, `IOHelper`, vertex puller, binding tables, metallib writer | SM1-3 decoder filling `SM50ShaderInternal` (the DXIL frontend's precedent) |
| Fixed function | `airconv/shaders/*.metal` helper mechanism | IR generator keyed by FF state hash |

Model the frontend on d3d11, not d3d12: D3D9 is immediate-mode, so emitting lambdas into `CommandChunk`s gets encoder merging, clears, present and queries for free. Follow d3d11 naming (`MTLD3D9Device`, `ComObject<>`, trailing-underscore members, own `.clang-format`).

## Phases

Each phase ends with something runnable. Slice commits like d3d12 did: target, then one stub per object, then implementations.

### 1. Skeleton
- `enable_d3d9` in `meson.options`, `subdir('d3d9')` in `src/meson.build`, meson.build copied from `src/d3d12/meson.build`.
- `d3d9.def`: `Direct3DCreate9`, `Direct3DCreate9Ex`, `D3DPERF_*`, `Direct3DShaderValidatorCreate9`.
- `IDirect3D9Ex`: adapter count/identifier, `GetDeviceCaps`, `CheckDeviceFormat`, `EnumAdapterModes` via `wsi_monitor`.
- Exit: a caps-query test app runs under Wine, 32-bit and 64-bit.

### 2. Device, clear, present
- `CreateDevice`: `CreateDXMTDevice`, swapchain from the focus/device window, implicit backbuffer and auto depth-stencil.
- `Clear`, `Present`, `PresentEx`, `GetBackBuffer`, `Reset` (rebuild swapchain, release DEFAULT-pool resources), `TestCooperativeLevel`.
- Backbuffer X8R8G8B8/A8R8G8B8 into the presenter's BGRA8 path; A2R10G10B10 to RGB10A2.
- Exit: a clear-color app presents at vsync.

### 3. Resources
- Pools: DEFAULT to private or renamable, MANAGED to private plus a CPU shadow, SYSTEMMEM and SCRATCH to staging only.
- `Lock` flags: `DISCARD` renames via `DynamicBuffer`, `NOOVERWRITE` maps the current allocation, `READONLY` skips upload.
- Surfaces, textures, cube and volume textures, `LockRect`/`LockBox`, `GetDC` later.
- Copies: `UpdateSurface`, `UpdateTexture`, `GetRenderTargetData`, `StretchRect` (blit or a shader for scaling/format change), `ColorFill`.
- Depth FOURCCs games probe for: INTZ, DF16, DF24, RAWZ (sampled depth); NULL render target.
- Exit: textured quad from a MANAGED texture with a vertex buffer.

### 4. SM1-3 decoder in airconv
- Refactors first (no behavior change, reviewable alone):
  - Pull call inlining out of `read_control_flow` (`dxbc_converter_cfg.cpp:673-750`).
  - Pull binding-table and reflection building out of `SM50Initialize` (`dxbc_converter.cpp:1006-1259`) so sampler type and shadow compare can be per-compile.
- Decoder: tokens into IR blocks. `c#`/`i#`/`b#` to cb0/cb1/cb2, `def` merged into the uploaded cb0 (relative addressing can reach them), `a0`/`aL`/`p0`/loop counters as int temps, `s#` to SRV+sampler pairs, `rep`/`loop`/`if_comp`/`setp` as hand-built blocks, predication via `InstMovConditional`.
- VS/PS linkage by semantic name (`color0`, `texcoord3`) so SM2 `oT#`/`oD#` and SM3 `dcl_*` outputs meet.
- Expand composite ops in the decoder (`pow`, `lrp`, `cmp`, `cnd`, `nrm`, `crs`, `lit`, `dst`, `sgn`, `m4x4`, `dp2add`).
- New IR flag for D3D9 multiply (0 x anything = 0); compare DXVK's `d3d9.floatEmulation`.
- Entry point `SM30Initialize` (or detect the 0xFFFE/0xFFFF version token), plus winemetal thunks (`airconv_thunks.c`, `unix/winemetal_unix.c`).
- `airconv_cli`: accept D3D9 bytecode and variant flags, so shaders can be tested offline against a corpus of fxc-compiled SM2/3 blobs.
- Exit: CLI compiles a vs_3_0/ps_3_0 corpus to metallib that passes `MTL_SHADER_VALIDATION` in a harness.

### 5. Draw path and state
- Vertex declaration to `SM50_SHADER_IA_INPUT_LAYOUT_DATA` (part of the VS variant key, as in d3d11). Puller gaps: int-to-float for UBYTE4/SHORT2/SHORT4, UDEC3, DEC3N with w = 1.
- FVF to a generated declaration.
- Render states into a PSO key (blend, color write, RT/DS formats, topology class), depth-stencil state cache, rasterizer command.
- Constants: per-draw upload of dirty `c#`/`i#`/`b#` ranges into the ring, bound as cb0-2.
- `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` through an internal ring buffer.
- Triangle fans: Metal has none, so rewrite to a triangle list index buffer (CPU for UP draws, compute for buffer draws).
- `SetStreamSourceFreq` instancing to step rate.
- Exit: a SM3 sample app and a simple real game's menu render.

### 6. D3D9 shader variants
New VS/PS variant structs, each with an `IOHelper` prologue/epilogue:

| Feature | Implementation |
|---|---|
| Alpha test | PS epilogue: compare oC0.a, discard |
| Fog (vertex, table, pixel) | fog interpolant plus PS epilogue using cb fog params |
| User clip planes | VS epilogue writing clip distances from a cb of planes |
| Point size, point sprites | new VS `OutputPointSize`, new PS `point_coord` input |
| Half-pixel offset | VS position epilogue; VPOS minus 0.5 in PS |
| sRGB write | pick sRGB RT view per draw (`Recall_sRGB`) |
| Sampler type, shadow compare | per-sampler variant for ps_1_x/2_0 without `dcl_2d`/`dcl_cube`; depth formats rewrite `texld` to `InstSampleCompare` |
| Flat shading | interpolation mode on COLOR inputs |

### 7. Fixed-function pipeline
- Generate IR directly from a hashed FF state: transform, lighting (8 lights), material source, texgen, texture transforms, vertex blending; PS texture stage combiner (8 stages), specular add, fog.
- Put lighting and combiner math in an `airconv/shaders/d3d9_ff.metal` helper linked on demand.
- Reference: DXVK `src/d3d9/d3d9_fixed_function.cpp` and wined3d `ffp` code for the exact semantics.
- Exit: FF-only samples from the DirectX SDK (Lights, Textures, Matrices) match reference images.

### 8. Remaining API surface
- Queries (occlusion via existing counter pool, event, timestamp), state blocks, `ProcessVertices` (CPU fallback), `D3DCREATE_MULTITHREADED` locking, Ex features (`WaitForVBlank`, `SetMaximumFrameLatency` onto `PresentBoundary`), `D3D9Options` from `Config` with `d3d9.*` keys.

## Status

All eight phases are implemented and covered by `tests/d3d9` and `tests/airconv_d3d9`. Wine's conformance suites run to the end; failures left (2026-10-29): visual 60, device 117, d3d9ex 31, stateblock 0 (`tests/d3d9/winetest.sh`). Not implemented:

- `ProcessVertices` with a vertex shader (fixed-function only).
- Shared resources (`pSharedHandle` other than D3D9Ex user memory) and `D3DFMT_A1`, so `ComposeRects` fails.
- YUY2/UYVY outside scratch memory (no GPU format or conversion).
- Not yet run against a real game.

## Testing
- `tests/d3d9/`: sample apps like `tests/dx11` (clear, triangle, FF lights, SM3 shading, UP draws, fans, point sprites, lock/discard).
- apitrace d3d9 traces from real games, replayed for regressions.
- Early game targets: something SM3 and 64-bit (simple), then a 32-bit Source engine title (TF2 is in the Neutron workflow), then an FF-heavy older title.

## Main risks
- **Shader compiler size.** The decoder plus variants is the bulk of the work; keep it on the existing IR so there is one backend.
- **PSO and variant explosion.** D3D9 state changes per draw; key carefully and compile on the thread pool as d3d11 does, accepting first-run stutter (~70 to 100 ms per Metal pipeline, per project-eve).
- **Intel Macs.** 16-bit packed formats only get capabilities on Apple7 (`dxmt_format.cpp:57-60`); Intel/AMD need conversion.
- **Behavior quirks** that DXVK learned per game (float emulation, half-pixel, NVDB/ATOC/RESZ driver hacks, `D3DLOCK` misuse). Reading DXVK's d3d9 config table saves rediscovering them.

## References
- DXVK d3d9 and dxso: https://github.com/doitsujin/dxvk/tree/master/src/d3d9, https://github.com/doitsujin/dxvk/tree/master/src/dxso
- Wine d3d9 and wined3d: https://gitlab.winehq.org/wine/wine/-/tree/master/dlls/d3d9
- SM1-3 token format: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/shader-code-tokens
- project-eve: `~/code/project-eve/graphics.md`, `options.md`
