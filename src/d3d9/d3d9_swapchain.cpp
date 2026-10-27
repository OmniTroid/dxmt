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

#include "d3d9_swapchain.hpp"
#include "d3d9_device.hpp"
#include "d3d9_format.hpp"
#include "config/config.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include "wsi_monitor.hpp"
#include "wsi_window.hpp"
#include <algorithm>
#include <cstring>

namespace dxmt {

static bool
IsSupportedBackBufferFormat(D3DFORMAT Format) {
  return Format == D3DFMT_X8R8G8B8 || Format == D3DFMT_A8R8G8B8 || Format == D3DFMT_A2R10G10B10;
}

MTLD3D9SwapChain::MTLD3D9SwapChain(MTLD3D9Device *pDevice, const D3DPRESENT_PARAMETERS &Params) :
    MTLD3D9DeviceChild(pDevice),
    parent_(pDevice),
    params_(Params),
    window_(Params.hDeviceWindow),
    monitor_(wsi::getWindowMonitor(Params.hDeviceWindow)) {
  native_view_ = WMT::CreateMetalViewFromHWND((intptr_t)window_, parent_->GetMTLDevice(), layer_weak_);
  if (!native_view_) {
    ERR("D3D9: failed to create a Metal view; Wine is missing the exports DXMT needs.");
    return;
  }

  uint8_t samples = params_.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? params_.MultiSampleType : 1;
  presenter_ = new Presenter(parent_->GetMTLDevice(), layer_weak_, parent_->queue().cmd_library, 1.0f, samples);

  wsi::WsiMode mode;
  if (wsi::getCurrentDisplayMode(monitor_, &mode) && mode.refreshRate.numerator && mode.refreshRate.denominator)
    refresh_rate_ = (double)mode.refreshRate.numerator / (double)mode.refreshRate.denominator;

  for (uint32_t i = 0; i < 256; i++) {
    WORD value = (WORD)(i * 0x101);
    gamma_ramp_.red[i] = value;
    gamma_ramp_.green[i] = value;
    gamma_ramp_.blue[i] = value;
  }
  gamma_lut_.version = 0;
}

MTLD3D9SwapChain::~MTLD3D9SwapChain() {
  backbuffers_.clear();
  if (native_view_) {
    parent_->WaitUntilGPUIdle();
    WMT::ReleaseMetalView(native_view_);
  }
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DSwapChain9) ||
      (parent_->IsExtended() && riid == __uuidof(IDirect3DSwapChain9Ex))) {
    *ppvObject = ref(this);
    return S_OK;
  }

  WARN("IDirect3DSwapChain9::QueryInterface: Unknown interface query ", str::format(riid));
  return E_NOINTERFACE;
}

HRESULT
MTLD3D9SwapChain::Reset(D3DPRESENT_PARAMETERS *pParams) {
  if (!presenter_)
    return D3DERR_NOTAVAILABLE;

  D3DPRESENT_PARAMETERS params = *pParams;
  if (!params.hDeviceWindow)
    params.hDeviceWindow = window_;

  if (params.Windowed && (params.BackBufferWidth == 0 || params.BackBufferHeight == 0)) {
    uint32_t width = 0, height = 0;
    wsi::getWindowSize(window_, &width, &height);
    if (!params.BackBufferWidth)
      params.BackBufferWidth = std::max(width, 1u);
    if (!params.BackBufferHeight)
      params.BackBufferHeight = std::max(height, 1u);
  }

  if (params.BackBufferFormat == D3DFMT_UNKNOWN && params.Windowed)
    params.BackBufferFormat = D3DFMT_X8R8G8B8;

  if (!IsSupportedBackBufferFormat(params.BackBufferFormat)) {
    ERR("D3D9: unsupported back buffer format ", params.BackBufferFormat);
    return D3DERR_INVALIDCALL;
  }

  if (params.BackBufferCount > D3DPRESENT_BACK_BUFFERS_MAX)
    return D3DERR_INVALIDCALL;
  if (params.BackBufferCount == 0)
    params.BackBufferCount = 1;

  if (!params.Windowed)
    WARN("D3D9: exclusive fullscreen is not implemented, presenting windowed");

  if (params.hDeviceWindow != window_)
    WARN("D3D9: changing the device window on Reset is not implemented");

  params.hDeviceWindow = window_;
  params_ = params;
  *pParams = params;

  HRESULT hr = CreateBackBuffers();
  if (FAILED(hr))
    return hr;

  ApplyLayerProps();
  return D3D_OK;
}

HRESULT
MTLD3D9SwapChain::CreateBackBuffers() {
  backbuffers_.clear();

  D3DSURFACE_DESC desc = {};
  desc.Format = params_.BackBufferFormat;
  desc.Type = D3DRTYPE_SURFACE;
  desc.Usage = D3DUSAGE_RENDERTARGET;
  desc.Pool = D3DPOOL_DEFAULT;
  desc.MultiSampleType = params_.MultiSampleType;
  desc.MultiSampleQuality = params_.MultiSampleQuality;
  desc.Width = params_.BackBufferWidth;
  desc.Height = params_.BackBufferHeight;

  // TODO: every back buffer gets its own storage but only the first is presented (no flip rotation)
  for (UINT i = 0; i < params_.BackBufferCount; i++) {
    Rc<Texture> texture;
    HRESULT hr = CreateD3D9SurfaceTexture(parent_, desc, texture);
    if (FAILED(hr))
      return hr;
    backbuffers_.emplace_back(new MTLD3D9Surface(parent_, desc, std::move(texture), this));
  }
  return D3D_OK;
}

void
MTLD3D9SwapChain::ApplyLayerProps() {
  WMTPixelFormat format = D3D9GetPixelFormat(parent_->GetMTLDevice(), params_.BackBufferFormat);
  uint8_t samples = params_.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? params_.MultiSampleType : 1;
  if (presenter_->changeLayerProperties(
          (WMTPixelFormat)ORIGINAL_FORMAT(format), WMTColorSpaceSRGB, params_.BackBufferWidth,
          params_.BackBufferHeight, samples
      ))
    parent_->WaitUntilGPUIdle();
}

UINT
MTLD3D9SwapChain::SyncInterval() const {
  switch (params_.PresentationInterval) {
  case D3DPRESENT_INTERVAL_IMMEDIATE:
    return 0;
  case D3DPRESENT_INTERVAL_TWO:
    return 2;
  case D3DPRESENT_INTERVAL_THREE:
    return 3;
  case D3DPRESENT_INTERVAL_FOUR:
    return 4;
  default:
    return 1;
  }
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::Present(
    const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion,
    DWORD dwFlags
) {
  if (!presenter_ || backbuffers_.empty())
    return D3DERR_INVALIDCALL;

  if (pSourceRect || pDestRect) {
    WARN_ONCE("D3D9: Present source/destination rects are ignored");
  }
  if (hDestWindowOverride && hDestWindowOverride != window_) {
    WARN_ONCE("D3D9: Present window override is ignored");
  }

  if (wsi::isMinimized(window_)) {
    parent_->FlushCommands();
    return parent_->IsExtended() ? S_PRESENT_OCCLUDED : D3D_OK;
  }

  UINT interval = (dwFlags & D3DPRESENT_FORCEIMMEDIATE) ? 0 : SyncInterval();
  double vsync_duration = interval / refresh_rate_;

  auto &queue = parent_->queue();
  auto chunk = queue.CurrentChunk();
  chunk->signal_frame_latency_fence_ = queue.CurrentFrameSeq();
  presenter_->changeGammaRamp(gamma_enabled_ ? &gamma_lut_ : nullptr);

  chunk->emitcc([backbuffer = backbuffers_[0]->texture(), presenter = presenter_, vsync_duration,
                 state = presenter_->synchronizeLayerProperties()](ArgumentEncodingContext &ctx) mutable {
    ctx.present(backbuffer, presenter, vsync_duration, state.metadata);
  });
  parent_->FlushCommands();
  queue.PresentBoundary();
  present_count_++;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetFrontBufferData(IDirect3DSurface9 *pDestSurface) {
  ERR_ONCE("IDirect3DSwapChain9::GetFrontBufferData: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) {
  InitReturnPtr(ppBackBuffer);
  if (!ppBackBuffer || iBackBuffer >= backbuffers_.size())
    return D3DERR_INVALIDCALL;
  *ppBackBuffer = ref(backbuffers_[iBackBuffer].ptr());
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) {
  if (!pRasterStatus)
    return D3DERR_INVALIDCALL;
  pRasterStatus->InVBlank = FALSE;
  pRasterStatus->ScanLine = 0;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetDisplayMode(D3DDISPLAYMODE *pMode) {
  if (!pMode)
    return D3DERR_INVALIDCALL;
  D3DDISPLAYMODEEX mode = {sizeof(mode)};
  HRESULT hr = GetDisplayModeEx(&mode, nullptr);
  if (FAILED(hr))
    return hr;
  pMode->Width = mode.Width;
  pMode->Height = mode.Height;
  pMode->RefreshRate = mode.RefreshRate;
  pMode->Format = mode.Format;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetDevice(IDirect3DDevice9 **ppDevice) {
  return GetDeviceInternal(ppDevice);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) {
  if (!pPresentationParameters)
    return D3DERR_INVALIDCALL;
  *pPresentationParameters = params_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetLastPresentCount(UINT *pLastPresentCount) {
  if (!pLastPresentCount)
    return D3DERR_INVALIDCALL;
  *pLastPresentCount = present_count_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetPresentStats(D3DPRESENTSTATS *pPresentationStatistics) {
  if (!pPresentationStatistics)
    return D3DERR_INVALIDCALL;
  std::memset(pPresentationStatistics, 0, sizeof(*pPresentationStatistics));
  pPresentationStatistics->PresentCount = present_count_;
  pPresentationStatistics->PresentRefreshCount = present_count_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetDisplayModeEx(D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) {
  return parent_->GetAdapter()->GetDisplayMode(pMode, pRotation);
}

void
MTLD3D9SwapChain::SetGammaRamp(DWORD Flags, const D3DGAMMARAMP *pRamp) {
  if (!pRamp)
    return;
  gamma_ramp_ = *pRamp;

  bool identity = true;
  for (uint32_t i = 0; i < 256 && identity; i++) {
    WORD value = (WORD)(i * 0x101);
    identity = pRamp->red[i] == value && pRamp->green[i] == value && pRamp->blue[i] == value;
  }
  gamma_enabled_ = !identity;
  if (!gamma_enabled_)
    return;

  auto sample = [](const WORD *ramp, uint32_t cp) {
    float pos = (float)cp * 255.0f / (float)(DXMT_GAMMA_CP_COUNT - 1);
    uint32_t lo = std::min((uint32_t)pos, 254u);
    float t = pos - (float)lo;
    return ((float)ramp[lo] * (1.0f - t) + (float)ramp[lo + 1] * t) / 65535.0f;
  };
  for (uint32_t i = 0; i < DXMT_GAMMA_CP_COUNT; i++) {
    gamma_lut_.red[i] = sample(pRamp->red, i);
    gamma_lut_.green[i] = sample(pRamp->green, i);
    gamma_lut_.blue[i] = sample(pRamp->blue, i);
  }
  gamma_lut_.version++;
}

void
MTLD3D9SwapChain::GetGammaRamp(D3DGAMMARAMP *pRamp) {
  if (pRamp)
    *pRamp = gamma_ramp_;
}

} // namespace dxmt
