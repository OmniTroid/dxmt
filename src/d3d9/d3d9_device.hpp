/*
 * Copyright 2026 David Skoland
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once

#include "d3d9_buffer.hpp"
#include "d3d9_constants.hpp"
#include "d3d9_interface.hpp"
#include "d3d9_shader_object.hpp"
#include "d3d9_vertex_declaration.hpp"
#include "dxmt_sampler.hpp"
#include "sha1/sha1_util.hpp"
#include <unordered_map>
#include "d3d9_surface.hpp"
#include "d3d9_swapchain.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_device.hpp"
#include <array>
#include <memory>
#include <mutex>

namespace dxmt {

class MTLD3D9Device final : public ComObjectWithInitialRef<IDirect3DDevice9Ex> {
public:
  MTLD3D9Device(
      MTLD3D9 *pParent, MTLD3D9Adapter *pAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
      bool Extended
  );
  ~MTLD3D9Device();

  HRESULT Initialize(D3DPRESENT_PARAMETERS *pParams, D3DDISPLAYMODEEX *pFullscreenMode);

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) final;
  HRESULT STDMETHODCALLTYPE TestCooperativeLevel() final;
  UINT STDMETHODCALLTYPE GetAvailableTextureMem() final;
  HRESULT STDMETHODCALLTYPE EvictManagedResources() final;
  HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 **d3d9) final;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *caps) final;
  HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT swapchain_idx, D3DDISPLAYMODE *mode) final;
  HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *parameters) final;
  HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface9 *bitmap) final;
  void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) final;
  BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) final;
  HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain9 **swapchain) final;
  HRESULT STDMETHODCALLTYPE GetSwapChain(UINT swapchain_idx, IDirect3DSwapChain9 **swapchain) final;
  UINT STDMETHODCALLTYPE GetNumberOfSwapChains() final;
  HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *parameters) final;
  HRESULT STDMETHODCALLTYPE Present(const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region) final;
  HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swapchain_idx, UINT backbuffer_idx, D3DBACKBUFFER_TYPE backbuffer_type, IDirect3DSurface9 **backbuffer) final;
  HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT swapchain_idx, D3DRASTER_STATUS *raster_status) final;
  HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL enable) final;
  void STDMETHODCALLTYPE SetGammaRamp(UINT swapchain_idx, DWORD flags, const D3DGAMMARAMP *ramp) final;
  void STDMETHODCALLTYPE GetGammaRamp(UINT swapchain_idx, D3DGAMMARAMP *ramp) final;
  HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT size, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **buffer, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, BOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, BOOL discard, IDirect3DSurface9 **surface, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) final;
  HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 *src_texture, IDirect3DBaseTexture9 *dst_texture) final;
  HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *render_target, IDirect3DSurface9 *dst_surface) final;
  HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT swapchain_idx, IDirect3DSurface9 *dst_surface) final;
  HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) final;
  HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR colour) final;
  HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle) final;
  HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD idx, IDirect3DSurface9 *surface) final;
  HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD idx, IDirect3DSurface9 **surface) final;
  HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *depth_stencil) final;
  HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 **depth_stencil) final;
  HRESULT STDMETHODCALLTYPE BeginScene() final;
  HRESULT STDMETHODCALLTYPE EndScene() final;
  HRESULT STDMETHODCALLTYPE Clear(DWORD rect_count, const D3DRECT *rects, DWORD flags, D3DCOLOR colour, float z, DWORD stencil) final;
  HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) final;
  HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *matrix) final;
  HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) final;
  HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *viewport) final;
  HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *viewport) final;
  HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *material) final;
  HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *material) final;
  HRESULT STDMETHODCALLTYPE SetLight(DWORD idx, const D3DLIGHT9 *light) final;
  HRESULT STDMETHODCALLTYPE GetLight(DWORD idx, D3DLIGHT9 *light) final;
  HRESULT STDMETHODCALLTYPE LightEnable(DWORD idx, BOOL enable) final;
  HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD idx, BOOL *enable) final;
  HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD idx, const float *plane) final;
  HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD idx, float *plane) final;
  HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) final;
  HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) final;
  HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **stateblock) final;
  HRESULT STDMETHODCALLTYPE BeginStateBlock() final;
  HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 **stateblock) final;
  HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *clip_status) final;
  HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *clip_status) final;
  HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture) final;
  HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) final;
  HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value) final;
  HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) final;
  HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD *value) final;
  HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD value) final;
  HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *pass_count) final;
  HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) final;
  HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT palette_idx, PALETTEENTRY *entries) final;
  HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT palette_idx) final;
  HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *palette_idx) final;
  HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *rect) final;
  HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *rect) final;
  HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL software) final;
  BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() final;
  HRESULT STDMETHODCALLTYPE SetNPatchMode(float segment_count) final;
  float STDMETHODCALLTYPE GetNPatchMode() final;
  HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count) final;
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, INT base_vertex_idx, UINT min_vertex_idx, UINT vertex_count, UINT start_idx, UINT primitive_count) final;
  HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride) final;
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) final;
  HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src_start_idx, UINT dst_idx, UINT vertex_count, IDirect3DVertexBuffer9 *dst_buffer, IDirect3DVertexDeclaration9 *declaration, DWORD flags) final;
  HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) final;
  HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) final;
  HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **declaration) final;
  HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) final;
  HRESULT STDMETHODCALLTYPE GetFVF(DWORD *fvf) final;
  HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *byte_code, IDirect3DVertexShader9 **shader) final;
  HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *shader) final;
  HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **shader) final;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT reg_idx, const float *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT reg_idx, float *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT reg_idx, const int *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT reg_idx, int *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT reg_idx, BOOL *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) final;
  HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) final;
  HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream_idx, UINT frequency) final;
  HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT stream_idx, UINT *frequency) final;
  HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *buffer) final;
  HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **buffer) final;
  HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *byte_code, IDirect3DPixelShader9 **shader) final;
  HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *shader) final;
  HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **shader) final;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT reg_idx, const float *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT reg_idx, float *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT reg_idx, const int *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT reg_idx, int *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT reg_idx, BOOL *data, UINT count) final;
  HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) final;
  HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) final;
  HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) final;
  HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query) final;
  HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT width, UINT height, float *rows, float *columns) final;
  HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9 *src_surface, IDirect3DSurface9 *dst_surface, IDirect3DVertexBuffer9 *src_descs, UINT rect_count, IDirect3DVertexBuffer9 *dst_descs, D3DCOMPOSERECTSOP operation, INT offset_x, INT offset_y) final;
  HRESULT STDMETHODCALLTYPE PresentEx(const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region, DWORD flags) final;
  HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT *priority) final;
  HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) final;
  HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT swapchain_idx) final;
  HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9 **resources, UINT32 resource_count) final;
  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT max_latency) final;
  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *max_latency) final;
  HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND dst_window) final;
  HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, BOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) final;
  HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT width, UINT Height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) final;
  HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, BOOL discard, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) final;
  HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS *parameters, D3DDISPLAYMODEEX *mode) final;
  HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT swapchain_idx, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation) final;

  WMT::Device
  GetMTLDevice() {
    return adapter_->GetMTLDevice();
  }

  Device &
  GetDXMTDevice() {
    return *dxmt_device_;
  }

  CommandQueue &
  queue() {
    return dxmt_device_->queue();
  }

  MTLD3D9Adapter *
  GetAdapter() {
    return adapter_;
  }

  bool
  IsExtended() const {
    return extended_;
  }

  template <CommandWithContext<ArgumentEncodingContext> cmd>
  void
  Emit(cmd &&fn) {
    queue().CurrentChunk()->emitcc(std::forward<cmd>(fn));
  }

  template <typename Fn>
  void
  EmitBlit(Fn &&fn) {
    EndRenderPass();
    Emit([fn = std::forward<Fn>(fn)](ArgumentEncodingContext &enc) mutable {
      enc.startBlitPass();
      fn(enc);
      enc.endPass();
    });
  }

  void FlushCommands();
  void WaitUntilGPUIdle();

  HRESULT MapStaging(Rc<StagingResource> &Staging, bool Read, bool Write, bool DoNotWait, void **ppData);

  // Box offsets are in pixels relative to the subresource; the buffer holds the whole subresource.
  void CopyTextureToBuffer(
      const Rc<Texture> &Src, uint32_t Level, uint32_t Slice, const D3DBOX &Box, const Rc<StagingResource> &Dst,
      const D3D9_FORMAT_LAYOUT &Layout
  );
  void CopyBufferToTexture(
      const Rc<StagingResource> &Src, const D3D9_FORMAT_LAYOUT &Layout, const Rc<Texture> &Dst, uint32_t Level,
      uint32_t Slice, const D3DBOX &Box, uint32_t DstX = ~0u, uint32_t DstY = ~0u
  );

  void EndRenderPass();

  std::unique_lock<std::recursive_mutex>
  Lock() {
    if (multithreaded_)
      return std::unique_lock<std::recursive_mutex>(mutex_);
    return std::unique_lock<std::recursive_mutex>();
  }

private:
  struct Stream {
    Com<MTLD3D9VertexBuffer, false> buffer;
    UINT offset = 0;
    UINT stride = 0;
    UINT frequency = 1;
  };

  struct BoundTexture {
    IDirect3DBaseTexture9 *texture = nullptr;
  };

  struct UpStream {
    Rc<Buffer> buffer;
    UINT offset = 0;
    UINT stride = 0;
  };

  bool BeginRenderPass();
  void UpdateRuntimeConstants();
  uint64_t PreAllocateArgumentBuffer(size_t Size, size_t Alignment);
  bool PrepareDraw(WMTPrimitiveTopologyClass Topology, uint32_t &InstanceCount);
  void BindStageResources(bool Pixel, MTLD3D9ShaderCode *Code, const float *FFConstants, UINT FFConstantCount);
  MTLD3D9ShaderCode *FixedFunctionShader(std::vector<DWORD> &&Bytecode, bool Pixel);
  MTLD3D9ShaderCode *FixedFunctionVS(float *Constants);
  MTLD3D9ShaderCode *FixedFunctionPS(float *Constants);
  WMT::RenderPipelineState GetPipelineState(WMTPrimitiveTopologyClass Topology, WMT::Function VS, WMT::Function PS);
  WMT::DepthStencilState GetDepthStencilState();
  Rc<Sampler> GetSampler(uint32_t Index);
  void ReleaseTextureBinding(BoundTexture &Binding);
  HRESULT DrawWithIndices(
      D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT StartIndex, UINT PrimitiveCount, Rc<Buffer> IndexBuffer,
      UINT IndexOffset, D3DFORMAT IndexFormat
  );
  HRESULT UploadToRing(
      std::unique_ptr<MTLD3D9BufferStorage> &Ring, UINT &RingOffset, const void *pData, UINT Size, UINT Alignment,
      Rc<Buffer> &OutBuffer, UINT &OutOffset
  );
  WMTPixelFormat RenderTargetFormat(MTLD3D9Surface *pSurface);
  TextureViewKey RenderTargetView(MTLD3D9Surface *pSurface);

  HRESULT ResetState(D3DPRESENT_PARAMETERS *pParams);
  void ClearBindings();
  HRESULT CreateAutoDepthStencil(const D3DPRESENT_PARAMETERS &Params);
  void SetDefaultViewport(MTLD3D9Surface *pTarget);
  void ClearSurface(MTLD3D9Surface *pSurface, const RECT &Rect, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);

  Com<MTLD3D9> parent_;
  MTLD3D9Adapter *adapter_;
  D3DDEVICE_CREATION_PARAMETERS creation_params_;
  bool extended_;
  bool multithreaded_;
  std::recursive_mutex mutex_;

  std::unique_ptr<Device> dxmt_device_;
  Com<MTLD3D9SwapChain, false> swapchain_;

  std::array<Com<MTLD3D9Surface, false>, caps::MaxSimultaneousRenderTargets> render_targets_;
  Com<MTLD3D9Surface, false> depth_stencil_;
  Com<MTLD3D9Surface, false> auto_depth_stencil_;

  D3DVIEWPORT9 viewport_ = {};
  RECT scissor_rect_ = {};
  std::array<DWORD, 256> render_states_ = {};
  bool in_scene_ = false;
  bool lost_ = false;

  std::vector<std::unique_ptr<MTLD3D9ShaderCode>> shader_codes_;
  Com<MTLD3D9VertexShader, false> vertex_shader_;
  Com<MTLD3D9PixelShader, false> pixel_shader_;
  Com<MTLD3D9VertexDeclaration, false> vertex_declaration_;
  std::unordered_map<DWORD, Com<MTLD3D9VertexDeclaration, false>> fvf_declarations_;
  DWORD fvf_ = 0;
  std::array<Stream, caps::MaxStreams> streams_;
  Com<MTLD3D9IndexBuffer, false> index_buffer_;

  // [0] vertex, [1] pixel; float, int and bool registers map to cbuffer slots 0, 1 and 2.
  std::array<std::array<float, 256 * 4>, 2> const_float_ = {};
  std::array<std::array<int32_t, 16 * 4>, 2> const_int_ = {};
  std::array<std::array<uint32_t, 16>, 2> const_bool_ = {};
  uint32_t const_dirty_[2] = {15, 15};
  std::unique_ptr<MTLD3D9BufferStorage> const_buffers_[2][4];
  SM30_RUNTIME_CONSTANTS runtime_constants_ = {};
  std::array<std::array<float, 4>, caps::MaxClipPlanes> clip_planes_ = {};

  // indexed by D3DTRANSFORMSTATETYPE; world matrices start at 256
  std::array<D3DMATRIX, 512> transforms_;
  std::array<std::array<DWORD, D3DTSS_CONSTANT + 1>, 8> texture_stage_states_;
  D3DMATERIAL9 material_ = {};
  std::vector<std::pair<D3DLIGHT9, bool>> lights_;
  std::unordered_map<Sha1Digest, std::unique_ptr<MTLD3D9ShaderCode>> ff_shaders_;
  std::unique_ptr<MTLD3D9BufferStorage> ff_constants_;
  UINT ff_constants_offset_ = 0;

  // pixel samplers 0-15, then vertex samplers D3DVERTEXTEXTURESAMPLER0-3 at 16-19
  std::array<BoundTexture, caps::MaxSamplersPS + caps::MaxSamplersVS> textures_;
  std::array<std::array<DWORD, D3DSAMP_DMAPOFFSET + 1>, caps::MaxSamplersPS + caps::MaxSamplersVS> sampler_states_;

  bool pass_active_ = false;
  uint64_t *pass_argbuf_size_ = nullptr;
  UpStream up_stream_;
  std::unordered_map<Sha1Digest, WMT::Reference<WMT::RenderPipelineState>> pipelines_;
  std::unordered_map<Sha1Digest, WMT::Reference<WMT::DepthStencilState>> depth_stencil_states_;
  std::unordered_map<Sha1Digest, Rc<Sampler>> samplers_;

  std::unique_ptr<MTLD3D9BufferStorage> up_vertices_;
  std::unique_ptr<MTLD3D9BufferStorage> up_indices_;
  UINT up_vertex_offset_ = 0;
  UINT up_index_offset_ = 0;
  UINT max_frame_latency_ = 3;
  INT gpu_thread_priority_ = 0;
};

HRESULT CreateD3D9Device(
    MTLD3D9 *pParent, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pParams, D3DDISPLAYMODEEX *pFullscreenMode, bool Extended, IDirect3DDevice9Ex **ppDevice
);

} // namespace dxmt
