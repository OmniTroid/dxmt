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
#include "log/log.hpp"

namespace dxmt {

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain9 **swapchain) {
  ERR_ONCE("MTLD3D9Device::CreateAdditionalSwapChain: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetFrontBufferData(UINT swapchain_idx, IDirect3DSurface9 *dst_surface) {
  ERR_ONCE("MTLD3D9Device::GetFrontBufferData: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) {
  ERR_ONCE("MTLD3D9Device::SetTransform: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *matrix) {
  ERR_ONCE("MTLD3D9Device::GetTransform: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) {
  ERR_ONCE("MTLD3D9Device::MultiplyTransform: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetMaterial(const D3DMATERIAL9 *material) {
  ERR_ONCE("MTLD3D9Device::SetMaterial: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetMaterial(D3DMATERIAL9 *material) {
  ERR_ONCE("MTLD3D9Device::GetMaterial: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetLight(DWORD idx, const D3DLIGHT9 *light) {
  ERR_ONCE("MTLD3D9Device::SetLight: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetLight(DWORD idx, D3DLIGHT9 *light) {
  ERR_ONCE("MTLD3D9Device::GetLight: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::LightEnable(DWORD idx, BOOL enable) {
  ERR_ONCE("MTLD3D9Device::LightEnable: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetLightEnable(DWORD idx, BOOL *enable) {
  ERR_ONCE("MTLD3D9Device::GetLightEnable: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetClipPlane(DWORD idx, const float *plane) {
  ERR_ONCE("MTLD3D9Device::SetClipPlane: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetClipPlane(DWORD idx, float *plane) {
  ERR_ONCE("MTLD3D9Device::GetClipPlane: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **stateblock) {
  ERR_ONCE("MTLD3D9Device::CreateStateBlock: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::BeginStateBlock() {
  ERR_ONCE("MTLD3D9Device::BeginStateBlock: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::EndStateBlock(IDirect3DStateBlock9 **stateblock) {
  ERR_ONCE("MTLD3D9Device::EndStateBlock: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetClipStatus(const D3DCLIPSTATUS9 *clip_status) {
  ERR_ONCE("MTLD3D9Device::SetClipStatus: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetClipStatus(D3DCLIPSTATUS9 *clip_status) {
  ERR_ONCE("MTLD3D9Device::GetClipStatus: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value) {
  ERR_ONCE("MTLD3D9Device::GetTextureStageState: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) {
  ERR_ONCE("MTLD3D9Device::SetTextureStageState: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ValidateDevice(DWORD *pass_count) {
  ERR_ONCE("MTLD3D9Device::ValidateDevice: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) {
  ERR_ONCE("MTLD3D9Device::SetPaletteEntries: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetPaletteEntries(UINT palette_idx, PALETTEENTRY *entries) {
  ERR_ONCE("MTLD3D9Device::GetPaletteEntries: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetCurrentTexturePalette(UINT palette_idx) {
  ERR_ONCE("MTLD3D9Device::SetCurrentTexturePalette: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetCurrentTexturePalette(UINT *palette_idx) {
  ERR_ONCE("MTLD3D9Device::GetCurrentTexturePalette: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetNPatchMode(float segment_count) {
  ERR_ONCE("MTLD3D9Device::SetNPatchMode: not implemented");
  return D3D_OK;
}

float STDMETHODCALLTYPE
MTLD3D9Device::GetNPatchMode() {
  ERR_ONCE("MTLD3D9Device::GetNPatchMode: not implemented");
  return 0;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ProcessVertices(UINT src_start_idx, UINT dst_idx, UINT vertex_count, IDirect3DVertexBuffer9 *dst_buffer, IDirect3DVertexDeclaration9 *declaration, DWORD flags) {
  ERR_ONCE("MTLD3D9Device::ProcessVertices: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) {
  ERR_ONCE("MTLD3D9Device::DrawRectPatch: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) {
  ERR_ONCE("MTLD3D9Device::DrawTriPatch: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DeletePatch(UINT handle) {
  ERR_ONCE("MTLD3D9Device::DeletePatch: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query) {
  ERR_ONCE("MTLD3D9Device::CreateQuery: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetConvolutionMonoKernel(UINT width, UINT height, float *rows, float *columns) {
  ERR_ONCE("MTLD3D9Device::SetConvolutionMonoKernel: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ComposeRects(IDirect3DSurface9 *src_surface, IDirect3DSurface9 *dst_surface, IDirect3DVertexBuffer9 *src_descs, UINT rect_count, IDirect3DVertexBuffer9 *dst_descs, D3DCOMPOSERECTSOP operation, INT offset_x, INT offset_y) {
  ERR_ONCE("MTLD3D9Device::ComposeRects: not implemented");
  return D3DERR_INVALIDCALL;
}

} // namespace dxmt
