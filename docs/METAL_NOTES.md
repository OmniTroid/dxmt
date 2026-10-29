# Metal notes

What DXMT's D3D9 work has learned about Metal, from Apple's headers and from experiments. Say which one each entry comes from, and add to it whenever Metal behaves unexpectedly.

## Where the documentation is

- API headers with Apple's doc comments: `$(xcrun --show-sdk-path)/System/Library/Frameworks/Metal.framework/Headers` (`MTLSampler.h`, `MTLTexture.h`, `MTLRenderPipeline.h`, ...).
- Metal Shading Language standard library: the Metal toolchain asset, found with
  ```sh
  find /private/var/run/com.apple.security.cryptexd/mnt -path '*include/metal/metal_stdlib'
  ```
  Per-type headers are in `__bits/` next to it (`metal_texture2d`, `metal_depth2d`, ...). They give signatures, not semantics.
- The MSL specification (PDF) and the API reference on developer.apple.com, for semantics the headers leave out.

## Samplers

- `compareFunction` defaults to `MTLCompareFunctionNever` (`MTLSampler.h`).
- `lodBias` is a sampler property only from macOS 26 (`MTLSampler.h`); DXMT applies bias in the shader from sampler metadata instead.
- Samplers placed in argument buffers need `supportArgumentBuffers` (`MTLSampler.h`).
- Experiment: `sample_compare` on D3D9 depth textures (Depth32Float_Stencil8 storage, bound through DXMT's argument buffers) returned 0 even with an ALWAYS compare function, while `sample` on the same texture and view returned the depth. Cause unknown; D3D9 shadow sampling compares in the shader (gather and blend for linear filtering).

## Textures

- Experiment: `gather` returns the bilinear footprint as (x0, y1), (x1, y1), (x1, y0), (x0, y0) with y0 the smaller v; AMD's FETCH4 order is that swizzled `.zxyw`. Wine's FETCH4 expectations confirmed it.
- `MTLTextureSwizzle` values are Zero 0, One 1, Red 2, Green 3, Blue 4, Alpha 5 (`MTLTexture.h`). DXMT packs a view swizzle into `WMTPixelFormat` bits 12-23 with `ENCODE_FORMAT_SWIZZLE(base, r, g, b, a)`; its result is an int, so parenthesize it inside a cast.
- Sampling a depth texture through a plain `texture2d<float>` view returns the depth in red (experiment, INTZ).
- DXMT: view key 0 is not a view; use `texture->fullView`.
- DXMT: textures that aren't shader-read-only have a fence tracker per level and slice, so `enc.access(texture, level, slice, ...)` orders only that subresource. Whole-texture operations (generateMipmaps) need `enc.access(texture, texture->fullView, ...)`.

## Pipelines and rasterization

- Experiment: pipeline creation fails ("Fragment input(s) ... not written by vertex shader") when a fragment input has no matching vertex output. D3D9 reads such inputs as 0, so the pixel shader variant leaves them out.
- Pixel centers are at +0.5; D3D9's are on integers, so the D3D9 viewport origin moves by +0.5.
- `depthClipMode` Clamp keeps geometry outside the depth range; D3D9 without D3DPMISCCAPS_CLIPTLVERTS wants that for every pretransformed draw, z-buffered or not (Wine's z_range_test, depth_clamp_test).
- Multisampled render passes need pipelines with a matching `rasterSampleCount`, including internal blits.
- Constant depth bias is in units of the minimum resolvable depth difference, which for float depth formats is 2^(e - 23) with e the exponent of the primitive's largest depth (the D3D10+ rule; Wine's test_depthbias fails with a fixed 2^24). Every D3D9 depth format but D16 is stored as float, so the pixel shader writes its depth plus D3DRS_DEPTHBIAS instead. Adding the bias to clip-space z in the vertex shader clipped geometry D3D9 keeps, because D3D9 biases after clipping (Wine's test_fog).
- Primitive restart is always on for strips: index 0xFFFF (16-bit) or 0xFFFFFFFF (32-bit) ends the strip, with no switch to disable it. D3D9 has no restart, so 16-bit strips containing 0xFFFF are redrawn with 32-bit indices (Wine's test_max_index16).
- Load actions only touch the render area (`renderTargetWidth`/`Height`), so a clear folded into a pass whose color target is smaller than the depth buffer leaves the rest uncleared. DXMT merges a clear into the following pass only when the pass covers the whole cleared texture.

## Building against a newer SDK

- APIs newer than the running macOS are weakly linked and NULL at runtime (`pipe2` crashed Wine this way). Check `nm -m` for unexpected `weak external` symbols after building.
