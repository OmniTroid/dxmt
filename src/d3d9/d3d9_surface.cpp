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
  if (container_ && forward_refs_)
    return container_->AddRef();
  return MTLD3D9Resource::AddRef();
}

ULONG STDMETHODCALLTYPE
MTLD3D9Surface::Release() {
  if (container_ && forward_refs_)
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
  // Windows XP accepts invalid rects and points at their corner, and some games (C&C3) rely on it; the whole
  // surface counts as written then
  RECT origin = rect;
  if (pRect) {
    origin = *pRect;
    bool valid = pRect->left >= 0 && pRect->top >= 0 && pRect->right <= rect.right && pRect->bottom <= rect.bottom &&
                 pRect->left < pRect->right && pRect->top < pRect->bottom;
    uint32_t mask = layout_.BlockSize - 1;
    if (valid && ((pRect->left & mask) || (pRect->top & mask)))
      return D3DERR_INVALIDCALL;
    if (valid)
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
  pLockedRect->pBits = (char *)data + (ptrdiff_t)(origin.top / (LONG)layout_.BlockSize) * pitch +
                       (ptrdiff_t)(origin.left / (LONG)layout_.BlockSize) * layout_.BlockBytes;
  locked_ = true;
  lock_flags_ = Flags;
  lock_rect_ = rect;
  if (container_ && !(Flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE))) {
    IDirect3DBaseTexture9 *texture = nullptr;
    if (SUCCEEDED(container_->QueryInterface(__uuidof(IDirect3DBaseTexture9), (void **)&texture))) {
      D3D9AddDirtyRegion(texture, level_, {(UINT)rect.left, (UINT)rect.top, (UINT)rect.right, (UINT)rect.bottom, 0, 1});
      texture->Release();
    }
  }
  return D3D_OK;
}

void
MTLD3D9Surface::MarkWritten() {
  if (level_ != 0 || !container_)
    return;
  IDirect3DBaseTexture9 *texture = nullptr;
  if (FAILED(container_->QueryInterface(__uuidof(IDirect3DBaseTexture9), (void **)&texture)))
    return;
  D3D9MarkMipsDirty(texture);
  texture->Release();
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::UnlockRect() {
  if (!locked_) {
    // unlocking an unlocked 2D texture level succeeds, unlike other surfaces
    IDirect3DTexture9 *texture = nullptr;
    if (container_ && SUCCEEDED(container_->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&texture))) {
      texture->Release();
      return D3D_OK;
    }
    return D3DERR_INVALIDCALL;
  }
  locked_ = false;
  if (!(lock_flags_ & D3DLOCK_READONLY))
    MarkWritten();
  shadow_->unmap();

  // managed texture levels upload their container's dirty region before the next draw instead
  bool deferred = desc_.Pool == D3DPOOL_MANAGED && container_;
  if (texture_ && !deferred && !(lock_flags_ & D3DLOCK_READONLY)) {
    uint32_t mask = layout_.BlockSize - 1;
    D3DBOX box = {
        (UINT)lock_rect_.left, (UINT)lock_rect_.top, std::min((UINT)(lock_rect_.right + mask) & ~mask, desc_.Width),
        std::min((UINT)(lock_rect_.bottom + mask) & ~mask, desc_.Height), 0, 1
    };
    parent_->CopyBufferToTexture(shadow_, layout_, texture_, level_, slice_, box);
  }
  return D3D_OK;
}

static UINT
DCBitsPerPixel(D3DFORMAT Format) {
  switch (Format) {
  case D3DFMT_R5G6B5:
  case D3DFMT_X1R5G5B5:
  case D3DFMT_A1R5G5B5:
    return 16;
  case D3DFMT_R8G8B8:
    return 24;
  case D3DFMT_X8R8G8B8:
  case D3DFMT_A8R8G8B8:
    return 32;
  default:
    return 0;
  }
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::GetDC(HDC *phdc) {
  if (!phdc)
    return D3DERR_INVALIDCALL;
  *phdc = nullptr;
  UINT bpp = DCBitsPerPixel(desc_.Format);
  if (!bpp || locked_ || dc_)
    return D3DERR_INVALIDCALL;

  // a DC is allowed on surfaces the app itself can't lock
  bool lockable = lockable_;
  lockable_ = true;
  HRESULT hr = LockRect(&dc_lock_, nullptr, 0);
  lockable_ = lockable;
  if (FAILED(hr))
    return hr;

  struct {
    BITMAPINFOHEADER header;
    DWORD masks[3];
  } info = {};
  info.header.biSize = sizeof(info.header);
  info.header.biWidth = desc_.Width;
  info.header.biHeight = -(LONG)desc_.Height;
  info.header.biPlanes = 1;
  info.header.biBitCount = bpp;
  info.header.biCompression = BI_RGB;
  if (desc_.Format == D3DFMT_R5G6B5) {
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = 0xF800;
    info.masks[1] = 0x07E0;
    info.masks[2] = 0x001F;
  }
  void *bits = nullptr;
  dib_ = CreateDIBSection(nullptr, (BITMAPINFO *)&info, DIB_RGB_COLORS, &bits, nullptr, 0);
  dc_ = CreateCompatibleDC(nullptr);
  if (!dib_ || !dc_) {
    if (dib_)
      DeleteObject(dib_);
    if (dc_)
      DeleteDC(dc_);
    dib_ = nullptr;
    dc_ = nullptr;
    UnlockRect();
    return D3DERR_INVALIDCALL;
  }
  dib_bits_ = bits;
  dib_pitch_ = (desc_.Width * bpp + 31) / 32 * 4;
  for (UINT y = 0; y < desc_.Height; y++)
    std::memcpy((char *)bits + y * dib_pitch_, (char *)dc_lock_.pBits + y * dc_lock_.Pitch, desc_.Width * bpp / 8);
  previous_bitmap_ = SelectObject(dc_, dib_);
  *phdc = dc_;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Surface::ReleaseDC(HDC hdc) {
  if (!dc_ || hdc != dc_)
    return D3DERR_INVALIDCALL;
  GdiFlush();
  UINT bytes = desc_.Width * DCBitsPerPixel(desc_.Format) / 8;
  for (UINT y = 0; y < desc_.Height; y++)
    std::memcpy((char *)dc_lock_.pBits + y * dc_lock_.Pitch, (char *)dib_bits_ + y * dib_pitch_, bytes);
  SelectObject(dc_, previous_bitmap_);
  DeleteDC(dc_);
  DeleteObject(dib_);
  dc_ = nullptr;
  dib_ = nullptr;
  return UnlockRect();
}

HRESULT
CreateD3D9SurfaceTexture(MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &OutTexture) {
  D3D9_TEXTURE_DESC desc = {D3DRTYPE_SURFACE, Desc.Width, Desc.Height, 1, 1, Desc.Usage, Desc.Format, Desc.Pool};
  uint32_t samples = Desc.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES ? Desc.MultiSampleType : 1;
  return CreateD3D9TextureStorage(pDevice, desc, 1, samples, OutTexture);
}

} // namespace dxmt
