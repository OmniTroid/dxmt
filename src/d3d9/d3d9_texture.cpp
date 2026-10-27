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

#include "d3d9_texture.hpp"
#include "d3d9_device.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>

namespace dxmt {

static bool
IsRenderableColor(MTLD3D9Device *pDevice, D3DFORMAT Format) {
  D3D9_FORMAT_DESC desc;
  if (!D3D9QueryFormat(Format, desc) || (desc.Flags & (D3D9_FORMAT_DEPTH | D3D9_FORMAT_BC)))
    return false;
  return int(pDevice->GetAdapter()->GetFormatCapability(Format)) & int(FormatCapability::Color);
}

HRESULT
CreateD3D9TextureStorage(
    MTLD3D9Device *pDevice, const D3D9_TEXTURE_DESC &Desc, UINT StorageLevels, uint32_t SampleCount,
    Rc<Texture> &OutTexture
) {
  WMTPixelFormat pixel_format = D3D9GetPixelFormat(pDevice->GetMTLDevice(), Desc.Format);
  if (pixel_format == WMTPixelFormatInvalid) {
    ERR("D3D9: unsupported texture format ", Desc.Format);
    return D3DERR_INVALIDCALL;
  }
  if (SampleCount > 1 && !pDevice->GetMTLDevice().supportsTextureSampleCount(SampleCount)) {
    ERR("D3D9: unsupported sample count ", SampleCount);
    return D3DERR_INVALIDCALL;
  }

  WMTTextureInfo info = {};
  info.pixel_format = (WMTPixelFormat)ORIGINAL_FORMAT(pixel_format);
  switch (Desc.Type) {
  case D3DRTYPE_CUBETEXTURE:
    info.type = WMTTextureTypeCube;
    break;
  case D3DRTYPE_VOLUMETEXTURE:
    info.type = WMTTextureType3D;
    break;
  default:
    info.type = SampleCount > 1 ? WMTTextureType2DMultisample : WMTTextureType2D;
    break;
  }
  info.width = Desc.Width;
  info.height = Desc.Height;
  info.depth = Desc.Type == D3DRTYPE_VOLUMETEXTURE ? Desc.Depth : 1;
  info.array_length = 1;
  info.mipmap_level_count = StorageLevels;
  info.sample_count = SampleCount;

  bool depth = Desc.Usage & D3DUSAGE_DEPTHSTENCIL;
  info.usage = WMTTextureUsageShaderRead;
  if (!depth)
    info.usage = (WMTTextureUsage)(info.usage | WMTTextureUsagePixelFormatView);
  if (depth || (Desc.Usage & D3DUSAGE_RENDERTARGET) ||
      (Desc.Type != D3DRTYPE_VOLUMETEXTURE && IsRenderableColor(pDevice, Desc.Format)))
    info.usage = (WMTTextureUsage)(info.usage | WMTTextureUsageRenderTarget);

  OutTexture = new Texture(info, pDevice->GetMTLDevice());

  Flags<TextureAllocationFlag> flags;
  flags.set(TextureAllocationFlag::GpuPrivate);
  OutTexture->rename(OutTexture->allocate(flags));
  for (uint32_t slice = 0; slice < OutTexture->arrayLength(); slice++)
    for (uint32_t level = 0; level < StorageLevels; level++)
      pDevice->queue().initializer.initWithZero(OutTexture.ptr(), OutTexture->current(), slice, level);
  return D3D_OK;
}

MTLD3D9Volume::MTLD3D9Volume(
    MTLD3D9Device *pDevice, const D3DVOLUME_DESC &Desc, Rc<Texture> Texture, IUnknown *pContainer, bool Lockable,
    uint32_t Level
) :
    MTLD3D9PrivateDataObject(pDevice),
    parent_(pDevice),
    desc_(Desc),
    texture_(std::move(Texture)),
    container_(pContainer),
    lockable_(Lockable || Desc.Pool != D3DPOOL_DEFAULT),
    level_(Level) {
  D3D9GetFormatLayout(pDevice->GetMTLDevice(), Desc.Format, layout_);
  if (Desc.Pool == D3DPOOL_SYSTEMMEM || Desc.Pool == D3DPOOL_SCRATCH)
    EnsureShadow();
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Volume::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DVolume9)) {
    *ppvObject = ref(this);
    return S_OK;
  }
  WARN("IDirect3DVolume9::QueryInterface: Unknown interface query ", str::format(riid));
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE
MTLD3D9Volume::AddRef() {
  return container_->AddRef();
}

ULONG STDMETHODCALLTYPE
MTLD3D9Volume::Release() {
  return container_->Release();
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Volume::GetContainer(REFIID riid, void **ppContainer) {
  if (!ppContainer)
    return D3DERR_INVALIDCALL;
  return container_->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Volume::GetDesc(D3DVOLUME_DESC *pDesc) {
  if (!pDesc)
    return D3DERR_INVALIDCALL;
  *pDesc = desc_;
  return D3D_OK;
}

Rc<StagingResource>
MTLD3D9Volume::EnsureShadow() {
  if (!shadow_ && layout_.BlockBytes) {
    uint32_t pitch = layout_.Pitch(desc_.Width);
    uint32_t image = pitch * layout_.Rows(desc_.Height);
    shadow_ = new StagingResource(parent_->GetMTLDevice(), (uint64_t)image * desc_.Depth, pitch, image);
  }
  return shadow_;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Volume::LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) {
  if (!pLockedVolume)
    return D3DERR_INVALIDCALL;
  pLockedVolume->pBits = nullptr;
  pLockedVolume->RowPitch = 0;
  pLockedVolume->SlicePitch = 0;

  if (!lockable_ || locked_ || !layout_.BlockBytes)
    return D3DERR_INVALIDCALL;

  D3DBOX box = {0, 0, desc_.Width, desc_.Height, 0, desc_.Depth};
  if (pBox) {
    if (pBox->Left >= pBox->Right || pBox->Top >= pBox->Bottom || pBox->Front >= pBox->Back ||
        pBox->Right > desc_.Width || pBox->Bottom > desc_.Height || pBox->Back > desc_.Depth)
      return D3DERR_INVALIDCALL;
    box = *pBox;
  }

  auto shadow = EnsureShadow();
  if (texture_ && desc_.Pool == D3DPOOL_DEFAULT && !(Flags & D3DLOCK_DISCARD))
    parent_->CopyTextureToBuffer(
        texture_, level_, 0, {0, 0, desc_.Width, desc_.Height, 0, desc_.Depth}, shadow, layout_
    );

  void *data = nullptr;
  HRESULT hr = parent_->MapStaging(
      shadow, !(Flags & D3DLOCK_DISCARD), !(Flags & D3DLOCK_READONLY), Flags & D3DLOCK_DONOTWAIT, &data
  );
  if (FAILED(hr))
    return hr;

  pLockedVolume->RowPitch = shadow->bytesPerRow;
  pLockedVolume->SlicePitch = shadow->bytesPerImage;
  pLockedVolume->pBits = (char *)data + (uint64_t)box.Front * shadow->bytesPerImage +
                         (box.Top / layout_.BlockSize) * shadow->bytesPerRow +
                         (box.Left / layout_.BlockSize) * layout_.BlockBytes;
  locked_ = true;
  lock_flags_ = Flags;
  lock_box_ = box;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Volume::UnlockBox() {
  if (!locked_)
    return D3DERR_INVALIDCALL;
  locked_ = false;
  shadow_->unmap();
  if (texture_ && !(lock_flags_ & D3DLOCK_READONLY)) {
    uint32_t mask = layout_.BlockSize - 1;
    D3DBOX box = lock_box_;
    box.Right = std::min((box.Right + mask) & ~mask, desc_.Width);
    box.Bottom = std::min((box.Bottom + mask) & ~mask, desc_.Height);
    parent_->CopyBufferToTexture(shadow_, layout_, texture_, level_, 0, box);
  }
  return D3D_OK;
}

template <typename Base>
MTLD3D9TextureBase<Base>::MTLD3D9TextureBase(
    MTLD3D9Device *pDevice, const D3D9_TEXTURE_DESC &Desc, Rc<Texture> &&Storage
) :
    MTLD3D9Resource<Base>(pDevice),
    parent_(pDevice),
    desc_(Desc),
    texture_(std::move(Storage)) {
  bool lockable = Desc.Usage & D3DUSAGE_DYNAMIC;
  if (Desc.Type == D3DRTYPE_VOLUMETEXTURE) {
    for (uint32_t level = 0; level < Desc.Levels; level++) {
      D3DVOLUME_DESC vd = {
          Desc.Format,
          D3DRTYPE_VOLUME,
          Desc.Usage,
          Desc.Pool,
          std::max(1u, Desc.Width >> level),
          std::max(1u, Desc.Height >> level),
          std::max(1u, Desc.Depth >> level)
      };
      volumes_.emplace_back(new MTLD3D9Volume(pDevice, vd, texture_, this, lockable, level));
    }
    return;
  }

  for (uint32_t face = 0; face < faces(); face++) {
    for (uint32_t level = 0; level < Desc.Levels; level++) {
      D3DSURFACE_DESC sd = {};
      sd.Format = Desc.Format;
      sd.Type = D3DRTYPE_SURFACE;
      sd.Usage = Desc.Usage;
      sd.Pool = Desc.Pool;
      sd.MultiSampleType = D3DMULTISAMPLE_NONE;
      sd.Width = std::max(1u, Desc.Width >> level);
      sd.Height = std::max(1u, Desc.Height >> level);
      surfaces_.emplace_back(new MTLD3D9Surface(pDevice, sd, Rc(texture_), this, lockable, level, face));
    }
  }
}

template <typename Base>
HRESULT STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
      riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(Base)) {
    *ppvObject = ref(this);
    return S_OK;
  }
  WARN("IDirect3DBaseTexture9::QueryInterface: Unknown interface query ", str::format(riid));
  return E_NOINTERFACE;
}

template <typename Base>
D3DRESOURCETYPE STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::GetType() {
  return desc_.Type;
}

template <typename Base>
DWORD STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::SetLOD(DWORD LODNew) {
  if (desc_.Pool != D3DPOOL_MANAGED)
    return 0;
  return std::exchange(lod_, std::min<DWORD>(LODNew, desc_.Levels - 1));
}

template <typename Base>
DWORD STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::GetLOD() {
  return lod_;
}

template <typename Base>
DWORD STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::GetLevelCount() {
  return desc_.Levels;
}

template <typename Base>
HRESULT STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) {
  if (FilterType == D3DTEXF_NONE)
    return D3DERR_INVALIDCALL;
  autogen_filter_ = FilterType;
  return D3D_OK;
}

template <typename Base>
D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::GetAutoGenFilterType() {
  return autogen_filter_;
}

template <typename Base>
void STDMETHODCALLTYPE
MTLD3D9TextureBase<Base>::GenerateMipSubLevels() {
  if (!(desc_.Usage & D3DUSAGE_AUTOGENMIPMAP) || !texture_ || texture_->miplevelCount() < 2)
    return;
  parent_->EmitBlit([texture = texture_](ArgumentEncodingContext &enc) {
    auto &cmd = enc.encodeBlitCommand<wmtcmd_blit_generate_mipmaps>();
    cmd.type = WMTBlitCommandGenerateMipmaps;
    cmd.texture = enc.access(texture, 0, 0, ResourceAccess::ReadWrite);
  });
}

template <typename Base>
void
MTLD3D9TextureBase<Base>::MarkMipsDirty() {
  GenerateMipSubLevels();
}

template <typename Base>
TextureViewKey
MTLD3D9TextureBase<Base>::SampleView(bool Srgb) {
  if (sample_view_valid_[Srgb])
    return sample_views_[Srgb];
  WMTPixelFormat format = D3D9GetPixelFormat(parent_->GetMTLDevice(), desc_.Format);
  if (Srgb) {
    auto swizzle = format & WMTPixelFormatCustomSwizzle;
    format = (WMTPixelFormat)(Recall_sRGB((WMTPixelFormat)ORIGINAL_FORMAT(format)) | swizzle);
  }
  sample_views_[Srgb] = texture_->createView({
      .format = format,
      .type = texture_->textureType(),
      .firstMiplevel = 0,
      .miplevelCount = texture_->miplevelCount(),
      .firstArraySlice = 0,
      .arraySize = texture_->arrayLength(),
  });
  sample_view_valid_[Srgb] = true;
  return sample_views_[Srgb];
}

template class MTLD3D9TextureBase<IDirect3DTexture9>;
template class MTLD3D9TextureBase<IDirect3DCubeTexture9>;
template class MTLD3D9TextureBase<IDirect3DVolumeTexture9>;

HRESULT STDMETHODCALLTYPE
MTLD3D9Texture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) {
  auto s = surface(0, Level);
  return s ? s->GetDesc(pDesc) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Texture::GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) {
  InitReturnPtr(ppSurfaceLevel);
  auto s = surface(0, Level);
  if (!s || !ppSurfaceLevel)
    return D3DERR_INVALIDCALL;
  *ppSurfaceLevel = ref(s);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Texture::LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) {
  auto s = surface(0, Level);
  return s ? s->LockRect(pLockedRect, pRect, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Texture::UnlockRect(UINT Level) {
  auto s = surface(0, Level);
  if (!s)
    return D3DERR_INVALIDCALL;
  HRESULT hr = s->UnlockRect();
  if (SUCCEEDED(hr) && Level == 0)
    MarkMipsDirty();
  return hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Texture::AddDirtyRect(const RECT *pDirtyRect) {
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9CubeTexture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) {
  auto s = surface(0, Level);
  return s ? s->GetDesc(pDesc) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9 **ppCubeMapSurface) {
  InitReturnPtr(ppCubeMapSurface);
  auto s = (uint32_t)FaceType < 6 ? surface(FaceType, Level) : nullptr;
  if (!s || !ppCubeMapSurface)
    return D3DERR_INVALIDCALL;
  *ppCubeMapSurface = ref(s);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9CubeTexture::LockRect(
    D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags
) {
  auto s = (uint32_t)FaceType < 6 ? surface(FaceType, Level) : nullptr;
  return s ? s->LockRect(pLockedRect, pRect, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9CubeTexture::UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) {
  auto s = (uint32_t)FaceType < 6 ? surface(FaceType, Level) : nullptr;
  if (!s)
    return D3DERR_INVALIDCALL;
  HRESULT hr = s->UnlockRect();
  if (SUCCEEDED(hr) && Level == 0)
    MarkMipsDirty();
  return hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9CubeTexture::AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT *pDirtyRect) {
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9VolumeTexture::GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) {
  auto v = volume(Level);
  return v ? v->GetDesc(pDesc) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9VolumeTexture::GetVolumeLevel(UINT Level, IDirect3DVolume9 **ppVolumeLevel) {
  InitReturnPtr(ppVolumeLevel);
  auto v = volume(Level);
  if (!v || !ppVolumeLevel)
    return D3DERR_INVALIDCALL;
  *ppVolumeLevel = ref(v);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9VolumeTexture::LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) {
  auto v = volume(Level);
  return v ? v->LockBox(pLockedVolume, pBox, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9VolumeTexture::UnlockBox(UINT Level) {
  auto v = volume(Level);
  if (!v)
    return D3DERR_INVALIDCALL;
  HRESULT hr = v->UnlockBox();
  if (SUCCEEDED(hr) && Level == 0)
    MarkMipsDirty();
  return hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9VolumeTexture::AddDirtyBox(const D3DBOX *pDirtyBox) {
  return D3D_OK;
}

static UINT
MaxLevels(UINT Width, UINT Height, UINT Depth) {
  UINT size = std::max({Width, Height, Depth});
  UINT levels = 1;
  while (size > 1) {
    size >>= 1;
    levels++;
  }
  return levels;
}

HRESULT
CreateD3D9Texture(MTLD3D9Device *pDevice, const D3D9_TEXTURE_DESC &Desc, IDirect3DBaseTexture9 **ppTexture) {
  InitReturnPtr(ppTexture);
  if (!ppTexture || !Desc.Width || !Desc.Height || !Desc.Depth)
    return D3DERR_INVALIDCALL;

  D3D9_FORMAT_DESC format;
  if (!D3D9QueryFormat(Desc.Format, format) || (format.Flags & D3D9_FORMAT_NULL_RT && Desc.Type != D3DRTYPE_TEXTURE))
    return D3DERR_INVALIDCALL;

  bool gpu_output = Desc.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
  if (gpu_output && Desc.Pool != D3DPOOL_DEFAULT)
    return D3DERR_INVALIDCALL;
  if ((Desc.Usage & D3DUSAGE_DYNAMIC) && Desc.Pool == D3DPOOL_MANAGED)
    return D3DERR_INVALIDCALL;
  if ((Desc.Usage & D3DUSAGE_DEPTHSTENCIL) && !(format.Flags & D3D9_FORMAT_DEPTH))
    return D3DERR_INVALIDCALL;
  if (Desc.Type == D3DRTYPE_VOLUMETEXTURE && (gpu_output || (format.Flags & D3D9_FORMAT_DEPTH)))
    return D3DERR_INVALIDCALL;

  D3D9_TEXTURE_DESC desc = Desc;
  UINT max_levels = MaxLevels(desc.Width, desc.Height, desc.Type == D3DRTYPE_VOLUMETEXTURE ? desc.Depth : 1);
  UINT storage_levels;
  if (desc.Usage & D3DUSAGE_AUTOGENMIPMAP) {
    if (desc.Levels > 1 || desc.Pool == D3DPOOL_SYSTEMMEM)
      return D3DERR_INVALIDCALL;
    desc.Levels = 1;
    storage_levels = max_levels;
  } else {
    if (desc.Levels > max_levels)
      return D3DERR_INVALIDCALL;
    if (!desc.Levels)
      desc.Levels = max_levels;
    storage_levels = desc.Levels;
  }

  Rc<Texture> storage;
  if (desc.Pool != D3DPOOL_SYSTEMMEM && desc.Pool != D3DPOOL_SCRATCH) {
    HRESULT hr = CreateD3D9TextureStorage(pDevice, desc, storage_levels, 1, storage);
    if (FAILED(hr))
      return hr;
  }

  switch (desc.Type) {
  case D3DRTYPE_TEXTURE:
    *ppTexture = ref(new MTLD3D9Texture(pDevice, desc, std::move(storage)));
    return D3D_OK;
  case D3DRTYPE_CUBETEXTURE:
    *ppTexture = ref(new MTLD3D9CubeTexture(pDevice, desc, std::move(storage)));
    return D3D_OK;
  case D3DRTYPE_VOLUMETEXTURE:
    *ppTexture = ref(new MTLD3D9VolumeTexture(pDevice, desc, std::move(storage)));
    return D3D_OK;
  default:
    return D3DERR_INVALIDCALL;
  }
}

} // namespace dxmt
