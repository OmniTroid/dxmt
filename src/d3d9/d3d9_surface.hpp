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

#include "d3d9_device_child.hpp"
#include "d3d9_format.hpp"
#include "dxmt_staging.hpp"
#include "dxmt_texture.hpp"

namespace dxmt {

class MTLD3D9Device;

// One mip level of one face. SYSTEMMEM/SCRATCH surfaces have no texture and live in the staging shadow only.
class MTLD3D9Surface final : public MTLD3D9Resource<IDirect3DSurface9> {
public:
  MTLD3D9Surface(
      MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &&pTexture, IUnknown *pContainer = nullptr,
      bool Lockable = false, uint32_t Level = 0, uint32_t Slice = 0
  );

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) final;
  ULONG STDMETHODCALLTYPE AddRef() final;
  ULONG STDMETHODCALLTYPE Release() final;

  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final;
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) final;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockRect() final;
  HRESULT STDMETHODCALLTYPE GetDC(HDC *phdc) final;
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) final;

  const D3DSURFACE_DESC &
  Desc() const {
    return desc_;
  }

  Rc<Texture>
  texture() const {
    return texture_;
  }

  TextureViewKey
  viewKey() const {
    return view_;
  }

  uint32_t
  level() const {
    return level_;
  }

  uint32_t
  slice() const {
    return slice_;
  }

  const D3D9_FORMAT_LAYOUT &
  layout() const {
    return layout_;
  }

  bool
  IsDepthStencil() const {
    return desc_.Usage & D3DUSAGE_DEPTHSTENCIL;
  }

  bool
  IsLocked() const {
    return locked_;
  }

  Rc<StagingResource> EnsureShadow();

private:
  MTLD3D9Device *parent_;
  D3DSURFACE_DESC desc_;
  Rc<Texture> texture_;
  TextureViewKey view_;
  IUnknown *container_;
  bool lockable_;
  uint32_t level_;
  uint32_t slice_;
  D3D9_FORMAT_LAYOUT layout_ = {0, 1};
  Rc<StagingResource> shadow_;
  bool locked_ = false;
  HDC dc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ previous_bitmap_ = nullptr;
  void *dib_bits_ = nullptr;
  UINT dib_pitch_ = 0;
  D3DLOCKED_RECT dc_lock_ = {};
  DWORD lock_flags_ = 0;
  RECT lock_rect_ = {};
};

HRESULT CreateD3D9SurfaceTexture(MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &OutTexture);

} // namespace dxmt
