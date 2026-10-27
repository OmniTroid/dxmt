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
#include "d3d9_texture.hpp"
#include "d3d9_format.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>

namespace dxmt {

MTLD3D9Surface::MTLD3D9Surface(
    MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &&pTexture, IUnknown *pContainer,
    bool Lockable, uint32_t Level, uint32_t Slice
) :
    MTLD3D9Resource(pDevice),
    parent_(pDevice),
    desc_(Desc),
    texture_(std::move(pTexture)),
    container_(pContainer),
    lockable_(Lockable || Desc.Pool != D3DPOOL_DEFAULT),
    level_(Level),
    slice_(Slice) {
  D3D9GetFormatLayout(pDevice->GetMTLDevice(), Desc.Format, layout_);

  if (texture_) {
    bool whole = texture_->miplevelCount() == 1 && texture_->arrayLength() == 1 &&
                 (texture_->textureType() == WMTTextureType2D || texture_->textureType() == WMTTextureType2DMultisample);
    if (whole) {
      view_ = texture_->fullView;
    } else {
      view_ = texture_->createView({
          .format = texture_->pixelFormat(),
          .type = WMTTextureType2D,
          .firstMiplevel = Level,
          .miplevelCount = 1,
          .firstArraySlice = Slice,
          .arraySize = 1,
      });
    }
  }

  if (Desc.Pool == D3DPOOL_SYSTEMMEM || Desc.Pool == D3DPOOL_SCRATCH)
    EnsureShadow();
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

Rc<StagingResource>
MTLD3D9Surface::EnsureShadow() {
  if (!shadow_ && layout_.BlockBytes) {
    uint32_t pitch = layout_.Pitch(desc_.Width);
    uint32_t image = pitch * layout_.Rows(desc_.Height);
    shadow_ = new StagingResource(parent_->GetMTLDevice(), image, pitch, image);
  }
  return shadow_;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) {
  if (!pLockedRect)
    return D3DERR_INVALIDCALL;
  pLockedRect->pBits = nullptr;
  pLockedRect->Pitch = 0;

  if (!lockable_ || locked_ || !layout_.BlockBytes)
    return D3DERR_INVALIDCALL;

  RECT rect = {0, 0, (LONG)desc_.Width, (LONG)desc_.Height};
  if (pRect) {
    if (pRect->left < 0 || pRect->top < 0 || pRect->right > rect.right || pRect->bottom > rect.bottom ||
        pRect->left >= pRect->right || pRect->top >= pRect->bottom)
      return D3DERR_INVALIDCALL;
    uint32_t mask = layout_.BlockSize - 1;
    if ((pRect->left & mask) || (pRect->top & mask))
      return D3DERR_INVALIDCALL;
    rect = *pRect;
  }

  auto shadow = EnsureShadow();
  bool read_back = texture_ && desc_.Pool == D3DPOOL_DEFAULT && !(Flags & D3DLOCK_DISCARD);
  if (read_back)
    parent_->CopyTextureToBuffer(texture_, level_, slice_, {0, 0, desc_.Width, desc_.Height, 0, 1}, shadow, layout_);

  void *data = nullptr;
  HRESULT hr = parent_->MapStaging(
      shadow, !(Flags & D3DLOCK_DISCARD), !(Flags & D3DLOCK_READONLY), Flags & D3DLOCK_DONOTWAIT, &data
  );
  if (FAILED(hr))
    return hr;

  uint32_t pitch = shadow->bytesPerRow;
  pLockedRect->Pitch = pitch;
  pLockedRect->pBits = (char *)data + (rect.top / layout_.BlockSize) * pitch +
                       (rect.left / layout_.BlockSize) * layout_.BlockBytes;
  locked_ = true;
  lock_flags_ = Flags;
  lock_rect_ = rect;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::UnlockRect() {
  if (!locked_)
    return D3DERR_INVALIDCALL;
  locked_ = false;
  shadow_->unmap();

  if (texture_ && !(lock_flags_ & D3DLOCK_READONLY)) {
    uint32_t mask = layout_.BlockSize - 1;
    D3DBOX box = {
        (UINT)lock_rect_.left, (UINT)lock_rect_.top, std::min((UINT)(lock_rect_.right + mask) & ~mask, desc_.Width),
        std::min((UINT)(lock_rect_.bottom + mask) & ~mask, desc_.Height), 0, 1
    };
    parent_->CopyBufferToTexture(shadow_, layout_, texture_, level_, slice_, box);
  }
  return D3D_OK;
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
  D3D9_TEXTURE_DESC desc = {D3DRTYPE_SURFACE, Desc.Width, Desc.Height, 1, 1, Desc.Usage, Desc.Format, Desc.Pool};
  uint32_t samples = Desc.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? Desc.MultiSampleType : 1;
  return CreateD3D9TextureStorage(pDevice, desc, 1, samples, OutTexture);
}

} // namespace dxmt
