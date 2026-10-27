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

#include "d3d9_surface.hpp"
#include "d3d9_device.hpp"
#include "d3d9_format.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

namespace dxmt {

MTLD3D9Surface::MTLD3D9Surface(
    MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &&pTexture, IUnknown *pContainer
) :
    MTLD3D9Resource(pDevice),
    desc_(Desc),
    texture_(std::move(pTexture)),
    container_(pContainer) {
  view_ = texture_->fullView;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) || riid == __uuidof(IDirect3DSurface9)) {
    *ppvObject = ref(this);
    return S_OK;
  }

  WARN("IDirect3DSurface9::QueryInterface: Unknown interface query ", str::format(riid));
  return E_NOINTERFACE;
}

// Surfaces owned by a swapchain or texture share the container's lifetime.
ULONG STDMETHODCALLTYPE
MTLD3D9Surface::AddRef() {
  if (container_)
    return container_->AddRef();
  return MTLD3D9Resource::AddRef();
}

ULONG STDMETHODCALLTYPE
MTLD3D9Surface::Release() {
  if (container_)
    return container_->Release();
  return MTLD3D9Resource::Release();
}

D3DRESOURCETYPE STDMETHODCALLTYPE
MTLD3D9Surface::GetType() {
  return D3DRTYPE_SURFACE;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::GetContainer(REFIID riid, void **ppContainer) {
  if (!ppContainer)
    return D3DERR_INVALIDCALL;
  if (container_)
    return container_->QueryInterface(riid, ppContainer);
  return device_->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::GetDesc(D3DSURFACE_DESC *pDesc) {
  if (!pDesc)
    return D3DERR_INVALIDCALL;
  *pDesc = desc_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) {
  ERR_ONCE("IDirect3DSurface9::LockRect: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::UnlockRect() {
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::GetDC(HDC *phdc) {
  ERR_ONCE("IDirect3DSurface9::GetDC: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::ReleaseDC(HDC hdc) {
  return D3DERR_INVALIDCALL;
}

HRESULT
CreateD3D9SurfaceTexture(MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &OutTexture) {
  WMTPixelFormat pixel_format = D3D9GetPixelFormat(pDevice->GetMTLDevice(), Desc.Format);
  if (pixel_format == WMTPixelFormatInvalid) {
    ERR("D3D9: unsupported surface format ", Desc.Format);
    return D3DERR_INVALIDCALL;
  }

  uint32_t samples = Desc.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? Desc.MultiSampleType : 1;
  if (samples > 1 && !pDevice->GetMTLDevice().supportsTextureSampleCount(samples)) {
    ERR("D3D9: unsupported sample count ", samples);
    return D3DERR_INVALIDCALL;
  }

  WMTTextureInfo info = {};
  info.pixel_format = (WMTPixelFormat)ORIGINAL_FORMAT(pixel_format);
  info.type = samples > 1 ? WMTTextureType2DMultisample : WMTTextureType2D;
  info.width = Desc.Width;
  info.height = Desc.Height;
  info.depth = 1;
  info.array_length = 1;
  info.mipmap_level_count = 1;
  info.sample_count = samples;
  info.usage = WMTTextureUsageShaderRead;
  if (Desc.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
    info.usage = (WMTTextureUsage)(info.usage | WMTTextureUsageRenderTarget);
  if (Desc.Usage & D3DUSAGE_RENDERTARGET)
    info.usage = (WMTTextureUsage)(info.usage | WMTTextureUsagePixelFormatView);

  OutTexture = new Texture(info, pDevice->GetMTLDevice());

  Flags<TextureAllocationFlag> flags;
  flags.set(TextureAllocationFlag::GpuPrivate);
  OutTexture->rename(OutTexture->allocate(flags));
  pDevice->queue().initializer.initWithZero(OutTexture.ptr(), OutTexture->current(), 0, 0);
  return D3D_OK;
}

} // namespace dxmt
