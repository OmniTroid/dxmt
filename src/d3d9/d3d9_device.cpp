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
#include "d3d9_format.hpp"
#include "d3d9_state.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>

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
  queue().CommitCurrentChunk();
}

void
MTLD3D9Device::WaitUntilGPUIdle() {
  uint64_t seq = queue().CurrentSeqId();
  FlushCommands();
  queue().WaitCPUFence(seq);
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
CreateStandaloneSurface(MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, IDirect3DSurface9 **ppSurface) {
  InitReturnPtr(ppSurface);
  if (!ppSurface || !Desc.Width || !Desc.Height)
    return D3DERR_INVALIDCALL;

  Rc<Texture> texture;
  HRESULT hr = CreateD3D9SurfaceTexture(pDevice, Desc, texture);
  if (FAILED(hr))
    return hr;
  *ppSurface = ref(new MTLD3D9Surface(pDevice, Desc, std::move(texture)));
  return D3D_OK;
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
  return CreateStandaloneSurface(this, desc, ppSurface);
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
  return CreateStandaloneSurface(this, desc, ppSurface);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget) {
  auto lock = Lock();
  if (RenderTargetIndex >= caps::MaxSimultaneousRenderTargets || (RenderTargetIndex == 0 && !pRenderTarget))
    return D3DERR_INVALIDCALL;

  auto surface = static_cast<MTLD3D9Surface *>(pRenderTarget);
  if (surface && !(surface->Desc().Usage & D3DUSAGE_RENDERTARGET))
    return D3DERR_INVALIDCALL;

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
