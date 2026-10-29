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

// CrossOver's macdrv get_win_data creates a client surface first, and for windows without macdrv data (the
// desktop window, no window) its cleanup left a win32u lock held and the next device creation deadlocked.
static WMT::Object
CreateViewForWindow(HWND Window, WMT::Device Device, WMT::MetalLayer &Layer) {
  if (!Window || Window == GetDesktopWindow())
    return {};
  return WMT::CreateMetalViewFromHWND((intptr_t)Window, Device, Layer);
}

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
  native_view_ = CreateViewForWindow(window_, parent_->GetMTLDevice(), layer_weak_);
  if (native_view_) {
    uint8_t samples = params_.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? params_.MultiSampleType : 1;
    presenter_ = new Presenter(parent_->GetMTLDevice(), layer_weak_, parent_->queue().cmd_library, 1.0f, samples);
  } else {
    WARN("D3D9: no Metal view for window ", (void *)window_, " (desktop or foreign window); presents are dropped");
  }

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
  if (fullscreen_) {
    wsi::restoreDisplayMode(monitor_);
    if (wsi::isWindow(window_))
      wsi::leaveFullscreenMode(window_, &window_state_, true);
  }
  backbuffers_.clear();
  if (native_view_ || !overrides_.empty())
    parent_->WaitUntilGPUIdle();
  for (auto &[window, target] : overrides_)
    WMT::ReleaseMetalView(target.view);
  if (native_view_)
    WMT::ReleaseMetalView(native_view_);
}

// Moves presentation to another window: a new Metal view and presenter, leaving fullscreen on the old one.
HRESULT
MTLD3D9SwapChain::AttachWindow(HWND Window) {
  if (!wsi::isWindow(Window))
    return D3DERR_INVALIDCALL;
  WMT::MetalLayer layer;
  WMT::Object view = CreateViewForWindow(Window, parent_->GetMTLDevice(), layer);
  if (!view)
    return D3DERR_NOTAVAILABLE;
  if (fullscreen_) {
    wsi::restoreDisplayMode(monitor_);
    wsi::leaveFullscreenMode(window_, &window_state_, true);
    fullscreen_ = false;
  }
  parent_->WaitUntilGPUIdle();
  if (native_view_)
    WMT::ReleaseMetalView(native_view_);
  native_view_ = view;
  layer_weak_ = layer;
  window_ = Window;
  monitor_ = wsi::getWindowMonitor(Window);
  presenter_ = new Presenter(parent_->GetMTLDevice(), layer_weak_, parent_->queue().cmd_library, 1.0f, 1);
  return D3D_OK;
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
  D3DPRESENT_PARAMETERS params = *pParams;
  if (!params.hDeviceWindow)
    params.hDeviceWindow = window_;
  if (params.hDeviceWindow != window_) {
    HRESULT hr = AttachWindow(params.hDeviceWindow);
    if (FAILED(hr))
      return hr;
  }

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
  // FLIPEX is a D3D9Ex swap effect, and COPY allows one back buffer (Wine's test_swapchain_parameters)
  bool flipex = params.SwapEffect == D3DSWAPEFFECT_FLIPEX && parent_->IsExtended();
  if ((params.SwapEffect < D3DSWAPEFFECT_DISCARD || params.SwapEffect > D3DSWAPEFFECT_COPY) && !flipex)
    return D3DERR_INVALIDCALL;
  if (params.SwapEffect == D3DSWAPEFFECT_COPY && params.BackBufferCount > 1)
    return D3DERR_INVALIDCALL;
  if (params.BackBufferCount == 0)
    params.BackBufferCount = 1;

  HRESULT mode_hr = SetFullscreen(params);
  if (FAILED(mode_hr))
    return mode_hr;
  params_ = params;
  *pParams = params;

  HRESULT hr = CreateBackBuffers();
  if (FAILED(hr))
    return hr;

  ApplyLayerProps();
  return D3D_OK;
}

// Exclusive fullscreen switches the display to the back buffer size and covers the monitor with the window.
HRESULT
MTLD3D9SwapChain::SetFullscreen(const D3DPRESENT_PARAMETERS &Params) {
  if (Params.Windowed) {
    if (!fullscreen_)
      return D3D_OK;
    wsi::restoreDisplayMode(monitor_);
    wsi::leaveFullscreenMode(window_, &window_state_, true);
    fullscreen_ = false;
  } else {
    monitor_ = wsi::getWindowMonitor(window_);
    wsi::WsiMode mode = {
        Params.BackBufferWidth, Params.BackBufferHeight, {Params.FullScreen_RefreshRateInHz, 1}, 32, false
    };
    if (!wsi::setWindowMode(monitor_, window_, mode))
      return D3DERR_NOTAVAILABLE;
    if (!fullscreen_ && !wsi::enterFullscreenMode(monitor_, window_, &window_state_, true))
      return D3DERR_NOTAVAILABLE;
    fullscreen_ = true;
  }
  wsi::WsiMode current;
  if (wsi::getCurrentDisplayMode(monitor_, &current) && current.refreshRate.numerator && current.refreshRate.denominator)
    refresh_rate_ = (double)current.refreshRate.numerator / (double)current.refreshRate.denominator;
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
    backbuffers_.emplace_back(new MTLD3D9Surface(
        parent_, desc, std::move(texture), this, params_.Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    ));
    backbuffers_.back()->KeepOwnRefcount();
  }
  return D3D_OK;
}

// Presenting to another window (hDestWindowOverride) goes through a Metal view and presenter of its own.
Rc<Presenter>
MTLD3D9SwapChain::OverridePresenter(HWND Window) {
  if (!wsi::isWindow(Window))
    return nullptr;
  auto &target = overrides_[Window];
  if (!target.presenter) {
    target.view = CreateViewForWindow(Window, parent_->GetMTLDevice(), target.layer);
    if (!target.view) {
      overrides_.erase(Window);
      return nullptr;
    }
    target.presenter =
        new Presenter(parent_->GetMTLDevice(), target.layer, parent_->queue().cmd_library, 1.0f, 1);
  }
  WMTPixelFormat format = D3D9GetPixelFormat(parent_->GetMTLDevice(), params_.BackBufferFormat);
  if (target.presenter->changeLayerProperties(
          (WMTPixelFormat)ORIGINAL_FORMAT(format), WMTColorSpaceSRGB, params_.BackBufferWidth,
          params_.BackBufferHeight, params_.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? params_.MultiSampleType : 1
      ))
    parent_->WaitUntilGPUIdle();
  return target.presenter;
}

void
MTLD3D9SwapChain::ApplyLayerProps() {
  WMTPixelFormat format = D3D9GetPixelFormat(parent_->GetMTLDevice(), params_.BackBufferFormat);
  uint8_t samples = params_.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? params_.MultiSampleType : 1;
  if (presenter_ && presenter_->changeLayerProperties(
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
  if (backbuffers_.empty())
    return D3DERR_INVALIDCALL;

  HWND target = hDestWindowOverride ? hDestWindowOverride : window_;
  Rc<Presenter> presenter = target == window_ ? presenter_ : OverridePresenter(target);
  if (!presenter) {
    if (target != window_)
      return D3DERR_INVALIDCALL;
    parent_->FlushCommands();
    return D3D_OK;
  }

  if (wsi::isMinimized(target)) {
    parent_->FlushCommands();
    return parent_->IsExtended() ? S_PRESENT_OCCLUDED : D3D_OK;
  }

  UINT interval = (dwFlags & D3DPRESENT_FORCEIMMEDIATE) ? 0 : SyncInterval();
  double vsync_duration = interval / refresh_rate_;

  parent_->EndRenderPass();
  Rc<Texture> presented = backbuffers_[0]->texture();
  if (pSourceRect || pDestRect) {
    HRESULT hr = ComposeRects(pSourceRect, pDestRect, target, presented);
    if (FAILED(hr))
      return hr;
  }
  if (keep_front_)
    CaptureFront(presented);
  auto &queue = parent_->queue();
  auto chunk = queue.CurrentChunk();
  chunk->signal_frame_latency_fence_ = queue.CurrentFrameSeq();
  presenter->changeGammaRamp(gamma_enabled_ ? &gamma_lut_ : nullptr);

  auto state = presenter->synchronizeLayerProperties();
  chunk->emitcc([backbuffer = std::move(presented), presenter = std::move(presenter), vsync_duration,
                 state = std::move(state)](ArgumentEncodingContext &ctx) mutable {
    ctx.present(backbuffer, presenter, vsync_duration, state.metadata);
  });
  parent_->FlushCommands();
  queue.PresentBoundary();
  present_count_++;
  return D3D_OK;
}

// Draws the source rect of the back buffer into the destination rect (window client coordinates) of a black
// back-buffer-sized texture, which is presented instead.
HRESULT
MTLD3D9SwapChain::ComposeRects(const RECT *pSourceRect, const RECT *pDestRect, HWND Window, Rc<Texture> &Out) {
  auto &bb = backbuffers_[0]->Desc();
  if (bb.MultiSampleType != D3DMULTISAMPLE_NONE) {
    WARN_ONCE("D3D9: Present rects with a multisampled back buffer are ignored");
    return D3D_OK;
  }
  RECT src = {0, 0, (LONG)bb.Width, (LONG)bb.Height};
  if (pSourceRect)
    src = *pSourceRect;
  uint32_t window_width = bb.Width, window_height = bb.Height;
  wsi::getWindowSize(Window, &window_width, &window_height);
  RECT dst = {0, 0, (LONG)window_width, (LONG)window_height};
  if (pDestRect)
    dst = *pDestRect;
  if (src.left >= src.right || src.top >= src.bottom || dst.left >= dst.right || dst.top >= dst.bottom)
    return D3DERR_INVALIDCALL;
  if (!composed_ || composed_->width() != bb.Width || composed_->height() != bb.Height) {
    D3DSURFACE_DESC desc = bb;
    HRESULT hr = CreateD3D9SurfaceTexture(parent_, desc, composed_);
    if (FAILED(hr))
      return hr;
  }
  float sx = float(bb.Width) / std::max(window_width, 1u), sy = float(bb.Height) / std::max(window_height, 1u);
  LONG x0 = std::clamp<LONG>(dst.left * sx, 0, bb.Width), y0 = std::clamp<LONG>(dst.top * sy, 0, bb.Height);
  LONG x1 = std::clamp<LONG>(dst.right * sx, x0, bb.Width), y1 = std::clamp<LONG>(dst.bottom * sy, y0, bb.Height);
  if (x1 == x0 || y1 == y0)
    return D3D_OK;
  WMTScissorRect dst_rect = {(uint64_t)x0, (uint64_t)y0, (uint64_t)(x1 - x0), (uint64_t)(y1 - y0)};
  parent_->Emit([src_texture = backbuffers_[0]->texture(), src_view = backbuffers_[0]->viewKey(), dst = composed_,
                 src_rect = std::array<float, 4>{(float)src.left, (float)src.top, (float)src.right, (float)src.bottom},
                 dst_rect, width = bb.Width, height = bb.Height](ArgumentEncodingContext &enc) mutable {
    auto view = dst->fullView;
    enc.clear_rt_cmd.begin(dst, view, 0);
    enc.clear_rt_cmd.clear(0, 0, width, height, 1, {0, 0, 0, 1});
    enc.clear_rt_cmd.end();
    float rect[4] = {src_rect[0], src_rect[1], src_rect[2], src_rect[3]};
    enc.blit_color_cmd.blit(src_texture, src_view, rect, dst, view, dst_rect, true);
  });
  Out = composed_;
  return D3D_OK;
}

HRESULT
MTLD3D9SwapChain::CaptureFront(const Rc<Texture> &Source) {
  auto &bb = backbuffers_[0]->Desc();
  if (!front_ || front_->width() != bb.Width || front_->height() != bb.Height) {
    D3DSURFACE_DESC desc = bb;
    desc.MultiSampleType = D3DMULTISAMPLE_NONE;
    desc.MultiSampleQuality = 0;
    HRESULT hr = CreateD3D9SurfaceTexture(parent_, desc, front_);
    if (FAILED(hr))
      return hr;
  }
  parent_->EndRenderPass();
  if (Source->sampleCount() > 1) {
    parent_->Emit([src = Source, src_view = backbuffers_[0]->viewKey(),
                   dst = front_](ArgumentEncodingContext &enc) mutable {
      auto view = dst->fullView;
      enc.resolveTexture(std::move(src), src_view, std::move(dst), view);
    });
    return D3D_OK;
  }
  parent_->EmitBlit([src = Source, dst = front_, width = bb.Width,
                     height = bb.Height](ArgumentEncodingContext &enc) {
    auto &cmd = enc.encodeBlitCommand<wmtcmd_blit_copy_from_texture_to_texture>();
    cmd.type = WMTBlitCommandCopyFromTextureToTexture;
    cmd.src = enc.access(src, 0, 0, ResourceAccess::Read);
    cmd.src_slice = 0;
    cmd.src_level = 0;
    cmd.src_origin = {0, 0, 0};
    cmd.src_size = {width, height, 1};
    cmd.dst = enc.access(dst, 0, 0, ResourceAccess::Write);
    cmd.dst_slice = 0;
    cmd.dst_level = 0;
    cmd.dst_origin = {0, 0, 0};
  });
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9SwapChain::GetFrontBufferData(IDirect3DSurface9 *pDestSurface) {
  auto lock = parent_->Lock();
  auto dst = static_cast<MTLD3D9Surface *>(pDestSurface);
  if (!dst || backbuffers_.empty())
    return D3DERR_INVALIDCALL;
  auto &dd = dst->Desc();
  auto &bb = backbuffers_[0]->Desc();
  if (dd.Pool != D3DPOOL_SYSTEMMEM || dd.Format != D3DFMT_A8R8G8B8 || dd.Width < bb.Width || dd.Height < bb.Height ||
      dst->IsLocked())
    return D3DERR_INVALIDCALL;
  // Before the first call nothing was captured, so the back buffer stands in for the last presented frame.
  if (!keep_front_) {
    keep_front_ = true;
    HRESULT hr = CaptureFront(backbuffers_[0]->texture());
    if (FAILED(hr))
      return hr;
  }
  Rc<Texture> source = front_;
  if (bb.Format != D3DFMT_X8R8G8B8 && bb.Format != D3DFMT_A8R8G8B8) {
    // the destination is always A8R8G8B8, so other back buffer formats are converted by drawing
    if (!front_8bit_ || front_8bit_->width() != bb.Width || front_8bit_->height() != bb.Height) {
      D3DSURFACE_DESC desc = bb;
      desc.Format = D3DFMT_A8R8G8B8;
      desc.MultiSampleType = D3DMULTISAMPLE_NONE;
      desc.MultiSampleQuality = 0;
      HRESULT hr = CreateD3D9SurfaceTexture(parent_, desc, front_8bit_);
      if (FAILED(hr))
        return hr;
    }
    parent_->EndRenderPass();
    parent_->Emit([src = front_, dst = front_8bit_, width = bb.Width, height = bb.Height](ArgumentEncodingContext &enc) {
      float rect[4] = {0, 0, (float)width, (float)height};
      enc.blit_color_cmd.blit(src, src->fullView, rect, dst, dst->fullView, {0, 0, width, height}, false);
    });
    source = front_8bit_;
  }
  D3DBOX box = {0, 0, bb.Width, bb.Height, 0, 1};
  parent_->CopyTextureToBuffer(source, 0, 0, box, dst->Shadow(), dst->layout());
  return D3D_OK;
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
