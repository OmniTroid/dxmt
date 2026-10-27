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
#include "dxmt_texture.hpp"

namespace dxmt {

class MTLD3D9Device;

class MTLD3D9Surface final : public MTLD3D9Resource<IDirect3DSurface9> {
public:
  MTLD3D9Surface(
      MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &&pTexture, IUnknown *pContainer = nullptr
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

  bool
  IsDepthStencil() const {
    return desc_.Usage & D3DUSAGE_DEPTHSTENCIL;
  }

private:
  D3DSURFACE_DESC desc_;
  Rc<Texture> texture_;
  TextureViewKey view_;
  IUnknown *container_;
};

HRESULT CreateD3D9SurfaceTexture(
    MTLD3D9Device *pDevice, const D3DSURFACE_DESC &Desc, Rc<Texture> &OutTexture
);

} // namespace dxmt
