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
MTLD3D9Device::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateTexture: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateVolumeTexture: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateCubeTexture(UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateCubeTexture: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVertexBuffer(UINT size, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateVertexBuffer: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateIndexBuffer(UINT size, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **buffer, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateIndexBuffer: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) {
  ERR_ONCE("MTLD3D9Device::UpdateSurface: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::UpdateTexture(IDirect3DBaseTexture9 *src_texture, IDirect3DBaseTexture9 *dst_texture) {
  ERR_ONCE("MTLD3D9Device::UpdateTexture: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetRenderTargetData(IDirect3DSurface9 *render_target, IDirect3DSurface9 *dst_surface) {
  ERR_ONCE("MTLD3D9Device::GetRenderTargetData: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetFrontBufferData(UINT swapchain_idx, IDirect3DSurface9 *dst_surface) {
  ERR_ONCE("MTLD3D9Device::GetFrontBufferData: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) {
  ERR_ONCE("MTLD3D9Device::StretchRect: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR colour) {
  ERR_ONCE("MTLD3D9Device::ColorFill: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle) {
  ERR_ONCE("MTLD3D9Device::CreateOffscreenPlainSurface: not implemented");
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
MTLD3D9Device::GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture) {
  ERR_ONCE("MTLD3D9Device::GetTexture: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) {
  ERR_ONCE("MTLD3D9Device::SetTexture: not implemented");
  return D3D_OK;
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
MTLD3D9Device::GetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD *value) {
  ERR_ONCE("MTLD3D9Device::GetSamplerState: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD value) {
  ERR_ONCE("MTLD3D9Device::SetSamplerState: not implemented");
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
MTLD3D9Device::DrawPrimitive(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count) {
  ERR_ONCE("MTLD3D9Device::DrawPrimitive: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, INT base_vertex_idx, UINT min_vertex_idx, UINT vertex_count, UINT start_idx, UINT primitive_count) {
  ERR_ONCE("MTLD3D9Device::DrawIndexedPrimitive: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride) {
  ERR_ONCE("MTLD3D9Device::DrawPrimitiveUP: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) {
  ERR_ONCE("MTLD3D9Device::DrawIndexedPrimitiveUP: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::ProcessVertices(UINT src_start_idx, UINT dst_idx, UINT vertex_count, IDirect3DVertexBuffer9 *dst_buffer, IDirect3DVertexDeclaration9 *declaration, DWORD flags) {
  ERR_ONCE("MTLD3D9Device::ProcessVertices: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) {
  ERR_ONCE("MTLD3D9Device::CreateVertexDeclaration: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) {
  ERR_ONCE("MTLD3D9Device::SetVertexDeclaration: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetVertexDeclaration(IDirect3DVertexDeclaration9 **declaration) {
  ERR_ONCE("MTLD3D9Device::GetVertexDeclaration: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetFVF(DWORD fvf) {
  ERR_ONCE("MTLD3D9Device::SetFVF: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetFVF(DWORD *fvf) {
  ERR_ONCE("MTLD3D9Device::GetFVF: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateVertexShader(const DWORD *byte_code, IDirect3DVertexShader9 **shader) {
  ERR_ONCE("MTLD3D9Device::CreateVertexShader: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetVertexShader(IDirect3DVertexShader9 *shader) {
  ERR_ONCE("MTLD3D9Device::SetVertexShader: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetVertexShader(IDirect3DVertexShader9 **shader) {
  ERR_ONCE("MTLD3D9Device::GetVertexShader: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetVertexShaderConstantF(UINT reg_idx, const float *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetVertexShaderConstantF: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetVertexShaderConstantF(UINT reg_idx, float *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetVertexShaderConstantF: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetVertexShaderConstantI(UINT reg_idx, const int *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetVertexShaderConstantI: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetVertexShaderConstantI(UINT reg_idx, int *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetVertexShaderConstantI: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetVertexShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetVertexShaderConstantB: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetVertexShaderConstantB(UINT reg_idx, BOOL *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetVertexShaderConstantB: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) {
  ERR_ONCE("MTLD3D9Device::SetStreamSource: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) {
  ERR_ONCE("MTLD3D9Device::GetStreamSource: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetStreamSourceFreq(UINT stream_idx, UINT frequency) {
  ERR_ONCE("MTLD3D9Device::SetStreamSourceFreq: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetStreamSourceFreq(UINT stream_idx, UINT *frequency) {
  ERR_ONCE("MTLD3D9Device::GetStreamSourceFreq: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetIndices(IDirect3DIndexBuffer9 *buffer) {
  ERR_ONCE("MTLD3D9Device::SetIndices: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetIndices(IDirect3DIndexBuffer9 **buffer) {
  ERR_ONCE("MTLD3D9Device::GetIndices: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreatePixelShader(const DWORD *byte_code, IDirect3DPixelShader9 **shader) {
  ERR_ONCE("MTLD3D9Device::CreatePixelShader: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetPixelShader(IDirect3DPixelShader9 *shader) {
  ERR_ONCE("MTLD3D9Device::SetPixelShader: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetPixelShader(IDirect3DPixelShader9 **shader) {
  ERR_ONCE("MTLD3D9Device::GetPixelShader: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetPixelShaderConstantF(UINT reg_idx, const float *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetPixelShaderConstantF: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetPixelShaderConstantF(UINT reg_idx, float *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetPixelShaderConstantF: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetPixelShaderConstantI(UINT reg_idx, const int *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetPixelShaderConstantI: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetPixelShaderConstantI(UINT reg_idx, int *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetPixelShaderConstantI: not implemented");
  return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetPixelShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::SetPixelShaderConstantB: not implemented");
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetPixelShaderConstantB(UINT reg_idx, BOOL *data, UINT count) {
  ERR_ONCE("MTLD3D9Device::GetPixelShaderConstantB: not implemented");
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

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateOffscreenPlainSurfaceEx(UINT width, UINT Height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) {
  ERR_ONCE("MTLD3D9Device::CreateOffscreenPlainSurfaceEx: not implemented");
  return D3DERR_INVALIDCALL;
}

} // namespace dxmt
