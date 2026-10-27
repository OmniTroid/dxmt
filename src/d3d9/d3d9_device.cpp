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

#include "d3d9_device.hpp"
#include "d3d9_buffer.hpp"
#include "d3d9_texture.hpp"
#include "d3d9_format.hpp"
#include "d3d9_state.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>
#include <cstring>

namespace dxmt {

MTLD3D9Device::MTLD3D9Device(
    MTLD3D9 *pParent, MTLD3D9Adapter *pAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    bool Extended
) :
    parent_(pParent),
    adapter_(pAdapter),
    extended_(Extended),
    multithreaded_(BehaviorFlags & D3DCREATE_MULTITHREADED) {
  creation_params_.AdapterOrdinal = pAdapter->GetOrdinal();
  creation_params_.DeviceType = DeviceType;
  creation_params_.hFocusWindow = hFocusWindow;
  creation_params_.BehaviorFlags = BehaviorFlags;

  dxmt_device_ = CreateDXMTDevice({.device = pAdapter->GetMTLDevice()});
}

MTLD3D9Device::~MTLD3D9Device() {
  ClearBindings();
  render_targets_ = {};
  depth_stencil_ = nullptr;
  auto_depth_stencil_ = nullptr;
  swapchain_ = nullptr;
  WaitUntilGPUIdle();
}

HRESULT
MTLD3D9Device::Initialize(D3DPRESENT_PARAMETERS *pParams, D3DDISPLAYMODEEX *pFullscreenMode) {
  if (!pParams)
    return D3DERR_INVALIDCALL;

  if (!pParams->hDeviceWindow)
    pParams->hDeviceWindow = creation_params_.hFocusWindow;
  if (!pParams->hDeviceWindow) {
    ERR("D3D9: CreateDevice without a device or focus window");
    return D3DERR_INVALIDCALL;
  }

  swapchain_ = new MTLD3D9SwapChain(this, *pParams);
  HRESULT hr = swapchain_->Reset(pParams);
  if (FAILED(hr))
    return hr;

  return ResetState(pParams);
}

HRESULT
MTLD3D9Device::CreateAutoDepthStencil(const D3DPRESENT_PARAMETERS &Params) {
  auto_depth_stencil_ = nullptr;
  if (!Params.EnableAutoDepthStencil)
    return D3D_OK;

  D3DSURFACE_DESC desc = {};
  desc.Format = Params.AutoDepthStencilFormat;
  desc.Type = D3DRTYPE_SURFACE;
  desc.Usage = D3DUSAGE_DEPTHSTENCIL;
  desc.Pool = D3DPOOL_DEFAULT;
  desc.MultiSampleType = Params.MultiSampleType;
  desc.MultiSampleQuality = Params.MultiSampleQuality;
  desc.Width = Params.BackBufferWidth;
  desc.Height = Params.BackBufferHeight;

  D3D9_FORMAT_DESC format;
  if (!D3D9QueryFormat(desc.Format, format) || !(format.Flags & D3D9_FORMAT_DEPTH))
    return D3DERR_INVALIDCALL;

  Rc<Texture> texture;
  HRESULT hr = CreateD3D9SurfaceTexture(this, desc, texture);
  if (FAILED(hr))
    return hr;
  auto_depth_stencil_ = new MTLD3D9Surface(this, desc, std::move(texture));
  return D3D_OK;
}

void
MTLD3D9Device::ClearBindings() {
  for (auto &binding : textures_)
    ReleaseTextureBinding(binding);
  vertex_shader_ = nullptr;
  pixel_shader_ = nullptr;
  vertex_declaration_ = nullptr;
  fvf_ = 0;
  for (auto &stream : streams_)
    stream = {};
  index_buffer_ = nullptr;
  for (auto &c : const_float_)
    c.fill(0.0f);
  for (auto &c : const_int_)
    c.fill(0);
  for (auto &c : const_bool_)
    c.fill(0);
  const_dirty_[0] = const_dirty_[1] = 7;
}

HRESULT
MTLD3D9Device::ResetState(D3DPRESENT_PARAMETERS *pParams) {
  HRESULT hr = CreateAutoDepthStencil(*pParams);
  if (FAILED(hr))
    return hr;

  render_targets_ = {};
  render_targets_[0] = swapchain_->BackBuffer(0);
  depth_stencil_ = auto_depth_stencil_;
  SetDefaultViewport(render_targets_[0].ptr());
  D3D9InitRenderStates(render_states_, pParams->EnableAutoDepthStencil);
  for (auto &ss : sampler_states_) {
    ss.fill(0);
    ss[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
    ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
    ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
    ss[D3DSAMP_MAXANISOTROPY] = 1;
  }
  ClearBindings();
  in_scene_ = false;
  lost_ = false;
  return D3D_OK;
}

void
MTLD3D9Device::SetDefaultViewport(MTLD3D9Surface *pTarget) {
  if (!pTarget)
    return;
  auto &desc = pTarget->Desc();
  viewport_ = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
  scissor_rect_ = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
}

void
MTLD3D9Device::FlushCommands() {
  EndRenderPass();
  queue().CommitCurrentChunk();
}

void
MTLD3D9Device::WaitUntilGPUIdle() {
  uint64_t seq = queue().CurrentSeqId();
  FlushCommands();
  queue().WaitCPUFence(seq);
}

HRESULT
MTLD3D9Device::MapStaging(Rc<StagingResource> &Staging, bool Read, bool Write, bool DoNotWait, void **ppData) {
  bool flushed = false;
  for (;;) {
    uint64_t coherent = queue().CoherentSeqId();
    auto result = Staging->tryMap(coherent, Read, Write);
    if (result == StagingMapResult::Mapped)
      return D3DERR_INVALIDCALL;
    if (result == StagingMapResult::Renamable) {
      auto next = Staging->allocate(coherent);
      std::memcpy(Staging->mappedMemory(next), Staging->mappedImmediateMemory(), Staging->length);
      Staging->updateImmediateName(queue().CurrentSeqId(), next);
      Emit([Staging, next](ArgumentEncodingContext &enc) mutable {
        auto _ = Staging->buffer()->rename(Staging->allocation(next));
      });
      result = StagingMapResult::Mappable;
    }
    if (result == StagingMapResult::Mappable) {
      *ppData = Staging->mappedImmediateMemory();
      return D3D_OK;
    }
    if (DoNotWait)
      return D3DERR_WASSTILLDRAWING;
    if (!flushed) {
      FlushCommands();
      flushed = true;
    }
    queue().WaitCPUFence(coherent + uint64_t(result));
  }
}

static uint64_t
BoxOffset(const D3DBOX &Box, const D3D9_FORMAT_LAYOUT &Layout, uint32_t BytesPerRow) {
  return (uint64_t)(Box.Top / Layout.BlockSize) * BytesPerRow + (Box.Left / Layout.BlockSize) * Layout.BlockBytes;
}

void
MTLD3D9Device::CopyTextureToBuffer(
    const Rc<Texture> &Src, uint32_t Level, uint32_t Slice, const D3DBOX &Box, const Rc<StagingResource> &Dst,
    const D3D9_FORMAT_LAYOUT &Layout
) {
  Dst->useCopyDestination(queue().CurrentSeqId());
  uint64_t offset = BoxOffset(Box, Layout, Dst->bytesPerRow);
  EmitBlit([=](ArgumentEncodingContext &enc) {
    auto src = enc.access(Src, Level, Slice, ResourceAccess::Read);
    auto [dst, dst_offset] = enc.access(Dst->buffer(), 0, Dst->length, ResourceAccess::Write);
    auto &cmd = enc.encodeBlitCommand<wmtcmd_blit_copy_from_texture_to_buffer>();
    cmd.type = WMTBlitCommandCopyFromTextureToBuffer;
    cmd.src = src;
    cmd.slice = Slice;
    cmd.level = Level;
    cmd.origin = {Box.Left, Box.Top, Box.Front};
    cmd.size = {Box.Right - Box.Left, Box.Bottom - Box.Top, Box.Back - Box.Front};
    cmd.dst = dst->buffer();
    cmd.offset = dst_offset + offset;
    cmd.bytes_per_row = Dst->bytesPerRow;
    cmd.bytes_per_image = Dst->bytesPerImage;
  });
}

void
MTLD3D9Device::CopyBufferToTexture(
    const Rc<StagingResource> &Src, const D3D9_FORMAT_LAYOUT &Layout, const Rc<Texture> &Dst, uint32_t Level,
    uint32_t Slice, const D3DBOX &Box, uint32_t DstX, uint32_t DstY
) {
  Src->useCopySource(queue().CurrentSeqId());
  uint64_t offset = BoxOffset(Box, Layout, Src->bytesPerRow);
  uint32_t x = DstX == ~0u ? Box.Left : DstX;
  uint32_t y = DstY == ~0u ? Box.Top : DstY;
  EmitBlit([=](ArgumentEncodingContext &enc) {
    auto [src, src_offset] = enc.access(Src->buffer(), 0, Src->length, ResourceAccess::Read);
    auto dst = enc.access(Dst, Level, Slice, ResourceAccess::Write);
    auto &cmd = enc.encodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_texture>();
    cmd.type = WMTBlitCommandCopyFromBufferToTexture;
    cmd.src = src->buffer();
    cmd.src_offset = src_offset + offset;
    cmd.bytes_per_row = Src->bytesPerRow;
    cmd.bytes_per_image = Src->bytesPerImage;
    cmd.size = {Box.Right - Box.Left, Box.Bottom - Box.Top, Box.Back - Box.Front};
    cmd.dst = dst;
    cmd.slice = Slice;
    cmd.level = Level;
    cmd.origin = {x, y, Box.Front};
  });
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DDevice9) ||
      (extended_ && riid == __uuidof(IDirect3DDevice9Ex))) {
    *ppvObject = ref(this);
    return S_OK;
  }

  WARN("IDirect3DDevice9::QueryInterface: Unknown interface query ", str::format(riid));
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::TestCooperativeLevel() {
  return lost_ ? D3DERR_DEVICENOTRESET : D3D_OK;
}

UINT STDMETHODCALLTYPE
MTLD3D9Device::GetAvailableTextureMem() {
  uint64_t bytes = GetMTLDevice().recommendedMaxWorkingSetSize();
  bytes = std::min<uint64_t>(bytes, 0xFFE00000ull);
  return (UINT)(bytes & ~0xFFFFFull);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::EvictManagedResources() {
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetDirect3D(IDirect3D9 **ppD3D9) {
  if (!ppD3D9)
    return D3DERR_INVALIDCALL;
  *ppD3D9 = parent_.ref();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetDeviceCaps(D3DCAPS9 *pCaps) {
  return adapter_->GetDeviceCaps(D3DDEVTYPE_HAL, pCaps);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE *pMode) {
  if (iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  return swapchain_->GetDisplayMode(pMode);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) {
  if (!pParameters)
    return D3DERR_INVALIDCALL;
  *pParameters = creation_params_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap) {
  WARN_ONCE("IDirect3DDevice9::SetCursorProperties: not implemented");
  return D3D_OK;
}

void STDMETHODCALLTYPE
MTLD3D9Device::SetCursorPosition(int X, int Y, DWORD Flags) {}

BOOL STDMETHODCALLTYPE
MTLD3D9Device::ShowCursor(BOOL bShow) {
  return FALSE;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9 **ppSwapChain) {
  InitReturnPtr(ppSwapChain);
  if (!ppSwapChain || iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  *ppSwapChain = ref(swapchain_.ptr());
  return D3D_OK;
}

UINT STDMETHODCALLTYPE
MTLD3D9Device::GetNumberOfSwapChains() {
  return 1;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) {
  auto lock = Lock();
  if (!pPresentationParameters)
    return D3DERR_INVALIDCALL;

  WaitUntilGPUIdle();
  render_targets_ = {};
  depth_stencil_ = nullptr;
  auto_depth_stencil_ = nullptr;

  HRESULT hr = swapchain_->Reset(pPresentationParameters);
  if (SUCCEEDED(hr))
    hr = ResetState(pPresentationParameters);
  if (FAILED(hr)) {
    lost_ = true;
    return D3DERR_INVALIDCALL;
  }
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion) {
  return PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, 0);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetBackBuffer(
    UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer
) {
  InitReturnPtr(ppBackBuffer);
  if (iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  return swapchain_->GetBackBuffer(iBackBuffer, Type, ppBackBuffer);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS *pRasterStatus) {
  if (iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  return swapchain_->GetRasterStatus(pRasterStatus);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetDialogBoxMode(BOOL bEnableDialogs) {
  return D3D_OK;
}

void STDMETHODCALLTYPE
MTLD3D9Device::SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP *pRamp) {
  if (iSwapChain == 0)
    swapchain_->SetGammaRamp(Flags, pRamp);
}

void STDMETHODCALLTYPE
MTLD3D9Device::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp) {
  if (iSwapChain == 0)
    swapchain_->GetGammaRamp(pRamp);
}

static HRESULT
CreateStandaloneSurface(
    MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, bool Lockable, IDirect3DSurface9 **ppSurface
) {
  InitReturnPtr(ppSurface);
  if (!ppSurface || !Desc.Width || !Desc.Height)
    return D3DERR_INVALIDCALL;

  Rc<Texture> texture;
  if (Desc.Pool != D3DPOOL_SYSTEMMEM && Desc.Pool != D3DPOOL_SCRATCH) {
    HRESULT hr = CreateD3D9SurfaceTexture(pDevice, Desc, texture);
    if (FAILED(hr))
      return hr;
  }
  *ppSurface = ref(new MTLD3D9Surface(pDevice, Desc, std::move(texture), nullptr, Lockable));
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateTexture(
    UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9 **ppTexture,
    HANDLE *pSharedHandle
) {
  auto lock = Lock();
  InitReturnPtr(ppTexture);
  if (pSharedHandle) {
    ERR("IDirect3DDevice9::CreateTexture: shared textures are not implemented");
    return D3DERR_INVALIDCALL;
  }
  D3D9_TEXTURE_DESC desc = {D3DRTYPE_TEXTURE, Width, Height, 1, Levels, Usage, Format, Pool};
  return CreateD3D9Texture(this, desc, reinterpret_cast<IDirect3DBaseTexture9 **>(ppTexture));
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateCubeTexture(
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9 **ppCubeTexture,
    HANDLE *pSharedHandle
) {
  auto lock = Lock();
  InitReturnPtr(ppCubeTexture);
  if (pSharedHandle)
    return D3DERR_INVALIDCALL;
  D3D9_TEXTURE_DESC desc = {D3DRTYPE_CUBETEXTURE, EdgeLength, EdgeLength, 1, Levels, Usage, Format, Pool};
  return CreateD3D9Texture(this, desc, reinterpret_cast<IDirect3DBaseTexture9 **>(ppCubeTexture));
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVolumeTexture(
    UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle
) {
  auto lock = Lock();
  InitReturnPtr(ppVolumeTexture);
  if (pSharedHandle)
    return D3DERR_INVALIDCALL;
  D3D9_TEXTURE_DESC desc = {D3DRTYPE_VOLUMETEXTURE, Width, Height, Depth, Levels, Usage, Format, Pool};
  return CreateD3D9Texture(this, desc, reinterpret_cast<IDirect3DBaseTexture9 **>(ppVolumeTexture));
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVertexBuffer(
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle
) {
  auto lock = Lock();
  InitReturnPtr(ppVertexBuffer);
  if (!ppVertexBuffer || !Length || pSharedHandle)
    return D3DERR_INVALIDCALL;
  D3DVERTEXBUFFER_DESC desc = {D3DFMT_VERTEXDATA, D3DRTYPE_VERTEXBUFFER, Usage, Pool, Length, FVF};
  *ppVertexBuffer = ref(new MTLD3D9VertexBuffer(this, desc));
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateIndexBuffer(
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9 **ppIndexBuffer,
    HANDLE *pSharedHandle
) {
  auto lock = Lock();
  InitReturnPtr(ppIndexBuffer);
  if (!ppIndexBuffer || !Length || pSharedHandle)
    return D3DERR_INVALIDCALL;
  if (Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32)
    return D3DERR_INVALIDCALL;
  D3DINDEXBUFFER_DESC desc = {Format, D3DRTYPE_INDEXBUFFER, Usage, Pool, Length};
  *ppIndexBuffer = ref(new MTLD3D9IndexBuffer(this, desc));
  return D3D_OK;
}

template <typename T>
static T *
AsTextureBase(IDirect3DBaseTexture9 *pTexture) {
  return static_cast<T *>(pTexture);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture) {
  auto lock = Lock();
  if (!pSourceTexture || !pDestinationTexture)
    return D3DERR_INVALIDCALL;
  D3DRESOURCETYPE type = pSourceTexture->GetType();
  if (type != pDestinationTexture->GetType())
    return D3DERR_INVALIDCALL;

  auto copy = [&](auto *src, auto *dst) -> HRESULT {
    auto &sd = src->Desc();
    auto &dd = dst->Desc();
    if (sd.Pool != D3DPOOL_SYSTEMMEM || dd.Pool != D3DPOOL_DEFAULT || sd.Format != dd.Format || !dst->texture())
      return D3DERR_INVALIDCALL;
    uint32_t offset = 0;
    while (offset < sd.Levels && std::max(1u, sd.Width >> offset) != dd.Width)
      offset++;
    if (offset == sd.Levels || sd.Levels - offset < (dd.Usage & D3DUSAGE_AUTOGENMIPMAP ? 1 : dd.Levels))
      return D3DERR_INVALIDCALL;

    uint32_t levels = dd.Usage & D3DUSAGE_AUTOGENMIPMAP ? 1 : dd.Levels;
    for (uint32_t level = 0; level < levels; level++) {
      if (type == D3DRTYPE_VOLUMETEXTURE) {
        auto sv = src->volume(level + offset);
        auto &vd = sv->Desc();
        CopyBufferToTexture(
            sv->EnsureShadow(), sv->layout(), dst->texture(), level, 0, {0, 0, vd.Width, vd.Height, 0, vd.Depth}
        );
        continue;
      }
      for (uint32_t face = 0; face < dst->faces(); face++) {
        auto ss = src->surface(face, level + offset);
        auto &desc = ss->Desc();
        CopyBufferToTexture(
            ss->EnsureShadow(), ss->layout(), dst->texture(), level, face, {0, 0, desc.Width, desc.Height, 0, 1}
        );
      }
    }
    dst->MarkMipsDirty();
    return D3D_OK;
  };

  switch (type) {
  case D3DRTYPE_TEXTURE:
    return copy(AsTextureBase<MTLD3D9Texture>(pSourceTexture), AsTextureBase<MTLD3D9Texture>(pDestinationTexture));
  case D3DRTYPE_CUBETEXTURE:
    return copy(
        AsTextureBase<MTLD3D9CubeTexture>(pSourceTexture), AsTextureBase<MTLD3D9CubeTexture>(pDestinationTexture)
    );
  case D3DRTYPE_VOLUMETEXTURE:
    return copy(
        AsTextureBase<MTLD3D9VolumeTexture>(pSourceTexture), AsTextureBase<MTLD3D9VolumeTexture>(pDestinationTexture)
    );
  default:
    return D3DERR_INVALIDCALL;
  }
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateRenderTarget(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Lockable, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle
) {
  return CreateRenderTargetEx(
      Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, 0
  );
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateRenderTargetEx(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Lockable, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle, DWORD Usage
) {
  auto lock = Lock();
  if (pSharedHandle) {
    ERR("IDirect3DDevice9::CreateRenderTarget: shared surfaces are not implemented");
    return D3DERR_INVALIDCALL;
  }

  D3D9_FORMAT_DESC format;
  if (!D3D9QueryFormat(Format, format) || (format.Flags & (D3D9_FORMAT_DEPTH | D3D9_FORMAT_BC)))
    return D3DERR_INVALIDCALL;

  D3DSURFACE_DESC desc = {};
  desc.Format = Format;
  desc.Type = D3DRTYPE_SURFACE;
  desc.Usage = D3DUSAGE_RENDERTARGET | (Usage & D3DUSAGE_RESTRICTED_CONTENT);
  desc.Pool = D3DPOOL_DEFAULT;
  desc.MultiSampleType = MultiSample;
  desc.MultiSampleQuality = MultisampleQuality;
  desc.Width = Width;
  desc.Height = Height;
  return CreateStandaloneSurface(this, desc, Lockable, ppSurface);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateDepthStencilSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle
) {
  return CreateDepthStencilSurfaceEx(
      Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle, 0
  );
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateDepthStencilSurfaceEx(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle, DWORD Usage
) {
  auto lock = Lock();
  if (pSharedHandle) {
    ERR("IDirect3DDevice9::CreateDepthStencilSurface: shared surfaces are not implemented");
    return D3DERR_INVALIDCALL;
  }

  D3D9_FORMAT_DESC format;
  if (!D3D9QueryFormat(Format, format) || !(format.Flags & D3D9_FORMAT_DEPTH))
    return D3DERR_INVALIDCALL;

  D3DSURFACE_DESC desc = {};
  desc.Format = Format;
  desc.Type = D3DRTYPE_SURFACE;
  desc.Usage = D3DUSAGE_DEPTHSTENCIL;
  desc.Pool = D3DPOOL_DEFAULT;
  desc.MultiSampleType = MultiSample;
  desc.MultiSampleQuality = MultisampleQuality;
  desc.Width = Width;
  desc.Height = Height;
  return CreateStandaloneSurface(this, desc, format.Flags & D3D9_FORMAT_LOCKABLE, ppSurface);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateOffscreenPlainSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle
) {
  return CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, ppSurface, pSharedHandle, 0);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateOffscreenPlainSurfaceEx(
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle,
    DWORD Usage
) {
  auto lock = Lock();
  if (pSharedHandle) {
    ERR("IDirect3DDevice9::CreateOffscreenPlainSurface: shared surfaces are not implemented");
    return D3DERR_INVALIDCALL;
  }
  if (Pool == D3DPOOL_MANAGED)
    return D3DERR_INVALIDCALL;

  D3D9_FORMAT_DESC format;
  if (!D3D9QueryFormat(Format, format) || (format.Flags & (D3D9_FORMAT_DEPTH | D3D9_FORMAT_NULL_RT)))
    return D3DERR_INVALIDCALL;

  D3DSURFACE_DESC desc = {};
  desc.Format = Format;
  desc.Type = D3DRTYPE_SURFACE;
  desc.Usage = Usage & D3DUSAGE_RESTRICTED_CONTENT;
  desc.Pool = Pool;
  desc.MultiSampleType = D3DMULTISAMPLE_NONE;
  desc.Width = Width;
  desc.Height = Height;
  return CreateStandaloneSurface(this, desc, true, ppSurface);
}

static D3DBOX
RectToBox(const RECT *pRect, const D3DSURFACE_DESC &Desc) {
  if (!pRect)
    return {0, 0, Desc.Width, Desc.Height, 0, 1};
  return {(UINT)pRect->left, (UINT)pRect->top, (UINT)pRect->right, (UINT)pRect->bottom, 0, 1};
}

static bool
BoxInside(const D3DBOX &Box, const D3DSURFACE_DESC &Desc) {
  return Box.Left < Box.Right && Box.Top < Box.Bottom && Box.Right <= Desc.Width && Box.Bottom <= Desc.Height;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface) {
  auto lock = Lock();
  auto src = static_cast<MTLD3D9Surface *>(pRenderTarget);
  auto dst = static_cast<MTLD3D9Surface *>(pDestSurface);
  if (!src || !dst || src == dst)
    return D3DERR_INVALIDCALL;

  auto &sd = src->Desc();
  auto &dd = dst->Desc();
  if (sd.Pool != D3DPOOL_DEFAULT || dd.Pool != D3DPOOL_SYSTEMMEM || !src->texture() || dst->IsLocked())
    return D3DERR_INVALIDCALL;
  if (sd.Width != dd.Width || sd.Height != dd.Height || sd.Format != dd.Format)
    return D3DERR_INVALIDCALL;
  if (sd.MultiSampleType != D3DMULTISAMPLE_NONE) {
    ERR_ONCE("IDirect3DDevice9::GetRenderTargetData: multisampled source is not implemented");
    return D3DERR_INVALIDCALL;
  }

  CopyTextureToBuffer(
      src->texture(), src->level(), src->slice(), RectToBox(nullptr, sd), dst->EnsureShadow(), dst->layout()
  );
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::UpdateSurface(
    IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestinationSurface,
    const POINT *pDestPoint
) {
  auto lock = Lock();
  auto src = static_cast<MTLD3D9Surface *>(pSourceSurface);
  auto dst = static_cast<MTLD3D9Surface *>(pDestinationSurface);
  if (!src || !dst)
    return D3DERR_INVALIDCALL;

  auto &sd = src->Desc();
  auto &dd = dst->Desc();
  if (sd.Pool != D3DPOOL_SYSTEMMEM || dd.Pool != D3DPOOL_DEFAULT || !dst->texture() || src->IsLocked())
    return D3DERR_INVALIDCALL;
  if (sd.Format != dd.Format || dd.MultiSampleType != D3DMULTISAMPLE_NONE)
    return D3DERR_INVALIDCALL;

  D3DBOX box = RectToBox(pSourceRect, sd);
  if (!BoxInside(box, sd))
    return D3DERR_INVALIDCALL;
  uint32_t x = pDestPoint ? pDestPoint->x : box.Left;
  uint32_t y = pDestPoint ? pDestPoint->y : box.Top;
  if (x + (box.Right - box.Left) > dd.Width || y + (box.Bottom - box.Top) > dd.Height)
    return D3DERR_INVALIDCALL;
  uint32_t mask = src->layout().BlockSize - 1;
  if ((box.Left | box.Top | x | y) & mask)
    return D3DERR_INVALIDCALL;

  CopyBufferToTexture(src->EnsureShadow(), src->layout(), dst->texture(), dst->level(), dst->slice(), box, x, y);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ColorFill(IDirect3DSurface9 *pSurface, const RECT *pRect, D3DCOLOR Color) {
  auto lock = Lock();
  auto surface = static_cast<MTLD3D9Surface *>(pSurface);
  if (!surface || surface->Desc().Pool != D3DPOOL_DEFAULT || !surface->texture() || surface->IsDepthStencil())
    return D3DERR_INVALIDCALL;
  if (!(surface->texture()->usage() & WMTTextureUsageRenderTarget)) {
    ERR_ONCE("IDirect3DDevice9::ColorFill: non-renderable format ", surface->Desc().Format);
    return D3DERR_INVALIDCALL;
  }

  D3DBOX box = RectToBox(pRect, surface->Desc());
  if (!BoxInside(box, surface->Desc()))
    return D3DERR_INVALIDCALL;
  RECT rect = {(LONG)box.Left, (LONG)box.Top, (LONG)box.Right, (LONG)box.Bottom};
  ClearSurface(surface, rect, D3DCLEAR_TARGET, Color, 0.0f, 0);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::StretchRect(
    IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestSurface,
    const RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter
) {
  auto lock = Lock();
  auto src = static_cast<MTLD3D9Surface *>(pSourceSurface);
  auto dst = static_cast<MTLD3D9Surface *>(pDestSurface);
  if (!src || !dst || src == dst || !src->texture() || !dst->texture())
    return D3DERR_INVALIDCALL;

  auto &sd = src->Desc();
  auto &dd = dst->Desc();
  if (sd.Pool != D3DPOOL_DEFAULT || dd.Pool != D3DPOOL_DEFAULT)
    return D3DERR_INVALIDCALL;

  D3DBOX sbox = RectToBox(pSourceRect, sd);
  D3DBOX dbox = RectToBox(pDestRect, dd);
  if (!BoxInside(sbox, sd) || !BoxInside(dbox, dd))
    return D3DERR_INVALIDCALL;

  bool same_size = sbox.Right - sbox.Left == dbox.Right - dbox.Left && sbox.Bottom - sbox.Top == dbox.Bottom - dbox.Top;
  bool same_format = ORIGINAL_FORMAT(src->texture()->pixelFormat()) == ORIGINAL_FORMAT(dst->texture()->pixelFormat());
  bool src_ms = sd.MultiSampleType != D3DMULTISAMPLE_NONE;
  bool dst_ms = dd.MultiSampleType != D3DMULTISAMPLE_NONE;

  if (src_ms && !dst_ms && same_format && same_size && !pSourceRect && !pDestRect && sd.Width == dd.Width &&
      sd.Height == dd.Height) {
    EndRenderPass();
    Emit([src = src->texture(), src_view = src->viewKey(), dst = dst->texture(),
          dst_view = dst->viewKey()](ArgumentEncodingContext &enc) mutable {
      enc.resolveTexture(std::move(src), src_view, std::move(dst), dst_view);
    });
    return D3D_OK;
  }

  if (same_size && same_format && src_ms == dst_ms) {
    EmitBlit([src = src->texture(), src_level = src->level(), src_slice = src->slice(), dst = dst->texture(),
              dst_level = dst->level(), dst_slice = dst->slice(), sbox, dbox](ArgumentEncodingContext &enc) {
      auto &cmd = enc.encodeBlitCommand<wmtcmd_blit_copy_from_texture_to_texture>();
      cmd.type = WMTBlitCommandCopyFromTextureToTexture;
      cmd.src = enc.access(src, src_level, src_slice, ResourceAccess::Read);
      cmd.src_slice = src_slice;
      cmd.src_level = src_level;
      cmd.src_origin = {sbox.Left, sbox.Top, 0};
      cmd.src_size = {sbox.Right - sbox.Left, sbox.Bottom - sbox.Top, 1};
      cmd.dst = enc.access(dst, dst_level, dst_slice, ResourceAccess::Write);
      cmd.dst_slice = dst_slice;
      cmd.dst_level = dst_level;
      cmd.dst_origin = {dbox.Left, dbox.Top, 0};
    });
    return D3D_OK;
  }

  ERR_ONCE("IDirect3DDevice9::StretchRect: scaling and format conversion are not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget) {
  auto lock = Lock();
  if (RenderTargetIndex >= caps::MaxSimultaneousRenderTargets || (RenderTargetIndex == 0 && !pRenderTarget))
    return D3DERR_INVALIDCALL;

  auto surface = static_cast<MTLD3D9Surface *>(pRenderTarget);
  if (surface && !(surface->Desc().Usage & D3DUSAGE_RENDERTARGET))
    return D3DERR_INVALIDCALL;

  if (render_targets_[RenderTargetIndex].ptr() != surface)
    EndRenderPass();
  render_targets_[RenderTargetIndex] = surface;
  if (RenderTargetIndex == 0)
    SetDefaultViewport(surface);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget) {
  auto lock = Lock();
  InitReturnPtr(ppRenderTarget);
  if (!ppRenderTarget || RenderTargetIndex >= caps::MaxSimultaneousRenderTargets)
    return D3DERR_INVALIDCALL;
  if (!render_targets_[RenderTargetIndex])
    return D3DERR_NOTFOUND;
  *ppRenderTarget = render_targets_[RenderTargetIndex].ref();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetDepthStencilSurface(IDirect3DSurface9 *pNewZStencil) {
  auto lock = Lock();
  auto surface = static_cast<MTLD3D9Surface *>(pNewZStencil);
  if (surface && !surface->IsDepthStencil())
    return D3DERR_INVALIDCALL;
  if (depth_stencil_.ptr() != surface)
    EndRenderPass();
  depth_stencil_ = surface;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetDepthStencilSurface(IDirect3DSurface9 **ppZStencilSurface) {
  auto lock = Lock();
  InitReturnPtr(ppZStencilSurface);
  if (!ppZStencilSurface)
    return D3DERR_INVALIDCALL;
  if (!depth_stencil_)
    return D3DERR_NOTFOUND;
  *ppZStencilSurface = depth_stencil_.ref();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::BeginScene() {
  auto lock = Lock();
  if (in_scene_)
    return D3DERR_INVALIDCALL;
  in_scene_ = true;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::EndScene() {
  auto lock = Lock();
  if (!in_scene_)
    return D3DERR_INVALIDCALL;
  in_scene_ = false;
  FlushCommands();
  return D3D_OK;
}

static bool
IntersectRect(RECT &Dst, const RECT &A, const RECT &B) {
  Dst.left = std::max(A.left, B.left);
  Dst.top = std::max(A.top, B.top);
  Dst.right = std::min(A.right, B.right);
  Dst.bottom = std::min(A.bottom, B.bottom);
  return Dst.left < Dst.right && Dst.top < Dst.bottom;
}

void
MTLD3D9Device::ClearSurface(
    MTLD3D9Surface *pSurface, const RECT &Rect, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil
) {
  EndRenderPass();
  auto &desc = pSurface->Desc();
  bool full = Rect.left == 0 && Rect.top == 0 && (UINT)Rect.right == desc.Width && (UINT)Rect.bottom == desc.Height;
  uint32_t x = Rect.left, y = Rect.top;
  uint32_t width = Rect.right - Rect.left, height = Rect.bottom - Rect.top;

  if (!pSurface->IsDepthStencil()) {
    WMTClearColor color = {
        ((Color >> 16) & 0xFF) / 255.0, ((Color >> 8) & 0xFF) / 255.0, (Color & 0xFF) / 255.0,
        ((Color >> 24) & 0xFF) / 255.0
    };
    if (full) {
      Emit([texture = pSurface->texture(), view = pSurface->viewKey(), color](ArgumentEncodingContext &enc) mutable {
        enc.clearColor(std::move(texture), view, 1, color);
      });
      return;
    }
    std::array<float, 4> rgba = {(float)color.r, (float)color.g, (float)color.b, (float)color.a};
    Emit([texture = pSurface->texture(), view = pSurface->viewKey(), x, y, width, height,
          rgba](ArgumentEncodingContext &enc) {
      enc.clear_rt_cmd.begin(texture, view, 0);
      enc.clear_rt_cmd.clear(x, y, width, height, 1, rgba);
      enc.clear_rt_cmd.end();
    });
    return;
  }

  unsigned planes = 0;
  if (Flags & D3DCLEAR_ZBUFFER)
    planes |= 1;
  if (Flags & D3DCLEAR_STENCIL)
    planes |= 2;

  Emit([texture = pSurface->texture(), view = pSurface->viewKey(), planes, full, x, y, width, height, Z,
        stencil = (uint8_t)Stencil](ArgumentEncodingContext &enc) mutable {
    unsigned flags = planes & DepthStencilPlanarFlags(texture->pixelFormat());
    if (!flags)
      return;
    if (full) {
      enc.clearDepthStencil(std::move(texture), view, 1, flags, Z, stencil);
      return;
    }
    enc.clear_rt_cmd.begin(texture, view, 0, flags);
    enc.clear_rt_cmd.clear(x, y, width, height, Z, stencil);
    enc.clear_rt_cmd.end();
  });
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
  auto lock = Lock();
  if (Count && !pRects)
    return D3DERR_INVALIDCALL;

  if (Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) {
    if (!depth_stencil_)
      return D3DERR_INVALIDCALL;
    D3D9_FORMAT_DESC format;
    D3D9QueryFormat(depth_stencil_->Desc().Format, format);
    if ((Flags & D3DCLEAR_STENCIL) && !(format.Flags & D3D9_FORMAT_STENCIL))
      return D3DERR_INVALIDCALL;
  }

  RECT area = {(LONG)viewport_.X, (LONG)viewport_.Y, (LONG)(viewport_.X + viewport_.Width),
               (LONG)(viewport_.Y + viewport_.Height)};
  if (render_states_[D3DRS_SCISSORTESTENABLE] && !IntersectRect(area, area, scissor_rect_))
    return D3D_OK;

  auto clear = [&](MTLD3D9Surface *surface, DWORD flags) {
    auto &desc = surface->Desc();
    RECT bounds = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
    RECT target;
    if (!IntersectRect(target, area, bounds))
      return;
    if (!Count) {
      ClearSurface(surface, target, flags, Color, Z, Stencil);
      return;
    }
    for (DWORD i = 0; i < Count; i++) {
      RECT rect = {pRects[i].x1, pRects[i].y1, pRects[i].x2, pRects[i].y2};
      if (IntersectRect(rect, rect, target))
        ClearSurface(surface, rect, flags, Color, Z, Stencil);
    }
  };

  if (Flags & D3DCLEAR_TARGET) {
    for (auto &rt : render_targets_)
      if (rt)
        clear(rt.ptr(), D3DCLEAR_TARGET);
  }
  if (Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
    clear(depth_stencil_.ptr(), Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL));

  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetViewport(const D3DVIEWPORT9 *pViewport) {
  auto lock = Lock();
  if (!pViewport)
    return D3DERR_INVALIDCALL;
  viewport_ = *pViewport;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetViewport(D3DVIEWPORT9 *pViewport) {
  auto lock = Lock();
  if (!pViewport)
    return D3DERR_INVALIDCALL;
  *pViewport = viewport_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) {
  auto lock = Lock();
  if ((uint32_t)State >= render_states_.size())
    return D3DERR_INVALIDCALL;
  if (State == D3DRS_SRGBWRITEENABLE && render_states_[State] != Value)
    EndRenderPass();
  render_states_[State] = Value;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) {
  auto lock = Lock();
  if (!pValue)
    return D3DERR_INVALIDCALL;
  if ((uint32_t)State >= render_states_.size()) {
    *pValue = 0;
    return D3DERR_INVALIDCALL;
  }
  *pValue = render_states_[State];
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetScissorRect(const RECT *pRect) {
  auto lock = Lock();
  if (!pRect)
    return D3DERR_INVALIDCALL;
  scissor_rect_ = *pRect;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetScissorRect(RECT *pRect) {
  auto lock = Lock();
  if (!pRect)
    return D3DERR_INVALIDCALL;
  *pRect = scissor_rect_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetSoftwareVertexProcessing(BOOL bSoftware) {
  return D3D_OK;
}

BOOL STDMETHODCALLTYPE
MTLD3D9Device::GetSoftwareVertexProcessing() {
  return FALSE;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::PresentEx(
    const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion,
    DWORD dwFlags
) {
  auto lock = Lock();
  return swapchain_->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetGPUThreadPriority(INT *pPriority) {
  if (!pPriority)
    return D3DERR_INVALIDCALL;
  *pPriority = gpu_thread_priority_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetGPUThreadPriority(INT Priority) {
  if (Priority < -7 || Priority > 7)
    return D3DERR_INVALIDCALL;
  gpu_thread_priority_ = Priority;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::WaitForVBlank(UINT iSwapChain) {
  if (iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CheckResourceResidency(IDirect3DResource9 **pResourceArray, UINT32 NumResources) {
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetMaximumFrameLatency(UINT MaxLatency) {
  auto lock = Lock();
  if (MaxLatency > 20)
    return D3DERR_INVALIDCALL;
  max_frame_latency_ = MaxLatency ? MaxLatency : 3;
  queue().SetMaxLatency(max_frame_latency_);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetMaximumFrameLatency(UINT *pMaxLatency) {
  if (!pMaxLatency)
    return D3DERR_INVALIDCALL;
  *pMaxLatency = max_frame_latency_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CheckDeviceState(HWND hDestinationWindow) {
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ResetEx(D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode) {
  return Reset(pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) {
  if (iSwapChain != 0)
    return D3DERR_INVALIDCALL;
  return swapchain_->GetDisplayModeEx(pMode, pRotation);
}

HRESULT
CreateD3D9Device(
    MTLD3D9 *pParent, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pParams, D3DDISPLAYMODEEX *pFullscreenMode, bool Extended, IDirect3DDevice9Ex **ppDevice
) {
  InitReturnPtr(ppDevice);
  if (!ppDevice || !pParams)
    return D3DERR_INVALIDCALL;

  auto adapter = pParent->GetAdapter(Adapter);
  if (!adapter)
    return D3DERR_INVALIDCALL;
  if (DeviceType != D3DDEVTYPE_HAL)
    return D3DERR_NOTAVAILABLE;

  auto device = new MTLD3D9Device(pParent, adapter, DeviceType, hFocusWindow, BehaviorFlags, Extended);
  HRESULT hr = device->Initialize(pParams, pFullscreenMode);
  if (FAILED(hr)) {
    device->Release();
    return hr;
  }
  *ppDevice = device;
  return D3D_OK;
}

} // namespace dxmt
