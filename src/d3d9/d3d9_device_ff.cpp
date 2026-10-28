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
#include "d3d9_fixed_function.hpp"
#include "d3d9_state_block.hpp"
#include "log/log.hpp"
#include <cmath>

namespace dxmt {

static D3DMATRIX
Multiply(const D3DMATRIX &A, const D3DMATRIX &B) {
  D3DMATRIX r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      float sum = 0;
      for (int k = 0; k < 4; k++)
        sum += A.m[i][k] * B.m[k][j];
      r.m[i][j] = sum;
    }
  return r;
}

static void
StoreColumns(float *Out, const D3DMATRIX &M, int Count) {
  for (int j = 0; j < Count; j++)
    for (int i = 0; i < 4; i++)
      Out[j * 4 + i] = M.m[i][j];
}

static void
StoreColor(float *Out, const D3DCOLORVALUE &C) {
  Out[0] = C.r;
  Out[1] = C.g;
  Out[2] = C.b;
  Out[3] = C.a;
}

static void
StoreColor(float *Out, D3DCOLOR C) {
  Out[0] = ((C >> 16) & 0xFF) / 255.0f;
  Out[1] = ((C >> 8) & 0xFF) / 255.0f;
  Out[2] = (C & 0xFF) / 255.0f;
  Out[3] = ((C >> 24) & 0xFF) / 255.0f;
}

MTLD3D9ShaderCode *
MTLD3D9Device::FixedFunctionShader(std::vector<DWORD> &&Bytecode, bool Pixel) {
  auto key = Sha1HashState().update(Bytecode.data(), Bytecode.size() * sizeof(DWORD)).update(Pixel).final();
  if (auto it = ff_shaders_.find(key); it != ff_shaders_.end())
    return it->second.get();
  std::unique_ptr<MTLD3D9ShaderCode> code;
  if (FAILED(MTLD3D9ShaderCode::Create(Bytecode.data(), Pixel, code)))
    return nullptr;
  return ff_shaders_.emplace(key, std::move(code)).first->second.get();
}

MTLD3D9ShaderCode *
MTLD3D9Device::FixedFunctionVS(float *C) {
  D3D9FFVertexState s = {};
  for (auto &e : vertex_declaration_->elements()) {
    switch (e.Usage) {
    case D3DDECLUSAGE_POSITIONT:
      s.pretransformed = true;
      break;
    case D3DDECLUSAGE_NORMAL:
      s.normal = e.UsageIndex == 0;
      break;
    case D3DDECLUSAGE_COLOR:
      if (e.UsageIndex == 0)
        s.diffuse = true;
      else if (e.UsageIndex == 1)
        s.specular = true;
      break;
    case D3DDECLUSAGE_TEXCOORD:
      if (e.UsageIndex < 8)
        s.texcoord_mask |= 1 << e.UsageIndex;
      break;
    }
  }
  s.lighting = render_states_[D3DRS_LIGHTING];
  auto source = [&](D3DRENDERSTATETYPE State) {
    auto value = (D3DMATERIALCOLORSOURCE)render_states_[State];
    if (!render_states_[D3DRS_COLORVERTEX] || (value == D3DMCS_COLOR1 && !s.diffuse) ||
        (value == D3DMCS_COLOR2 && !s.specular))
      return D3DMCS_MATERIAL;
    return value;
  };
  s.diffuse_source = source(D3DRS_DIFFUSEMATERIALSOURCE);
  s.ambient_source = source(D3DRS_AMBIENTMATERIALSOURCE);
  s.emissive_source = source(D3DRS_EMISSIVEMATERIALSOURCE);

  auto &world = transforms_[D3DTS_WORLDMATRIX(0)];
  StoreColumns(C + 4 * ff::VSWorldViewProj, Multiply(Multiply(world, transforms_[D3DTS_VIEW]), transforms_[D3DTS_PROJECTION]), 4);
  StoreColumns(C + 4 * ff::VSWorld, world, 3);

  float depth = viewport_.MaxZ - viewport_.MinZ;
  float depth_scale = depth != 0.0f ? 1.0f / depth : 1.0f;
  float *scale = C + 4 * ff::VSViewportScale, *offset = C + 4 * ff::VSViewportOffset;
  scale[0] = 2.0f / viewport_.Width;
  scale[1] = -2.0f / viewport_.Height;
  scale[2] = depth_scale;
  offset[0] = -1.0f - 2.0f * viewport_.X / viewport_.Width;
  offset[1] = 1.0f + 2.0f * viewport_.Y / viewport_.Height;
  offset[2] = -viewport_.MinZ * depth_scale;

  StoreColor(C + 4 * ff::VSMaterialDiffuse, material_.Diffuse);
  StoreColor(C + 4 * ff::VSMaterialAmbient, material_.Ambient);
  StoreColor(C + 4 * ff::VSMaterialSpecular, material_.Specular);
  StoreColor(C + 4 * ff::VSMaterialEmissive, material_.Emissive);
  float *ambient = C + 4 * ff::VSAmbient;
  StoreColor(ambient, (D3DCOLOR)render_states_[D3DRS_AMBIENT]);

  if (s.lighting && render_states_[D3DRS_SPECULARENABLE]) {
    WARN_ONCE("D3D9: fixed-function specular lighting is not implemented");
  }
  for (auto &[light, enabled] : lights_) {
    if (!enabled || s.light_count == ff::MaxLights)
      continue;
    ambient[0] += light.Ambient.r;
    ambient[1] += light.Ambient.g;
    ambient[2] += light.Ambient.b;
    if (light.Type != D3DLIGHT_DIRECTIONAL) {
      WARN_ONCE("D3D9: fixed-function point and spot lights are not implemented");
      continue;
    }
    float *l = C + 4 * (ff::VSLightBase + 2 * s.light_count);
    auto &d = light.Direction;
    float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length > 0) {
      l[0] = -d.x / length;
      l[1] = -d.y / length;
      l[2] = -d.z / length;
    }
    StoreColor(l + 4, light.Diffuse);
    s.light_count++;
  }
  return FixedFunctionShader(D3D9GenerateFixedFunctionVS(s), false);
}

MTLD3D9ShaderCode *
MTLD3D9Device::FixedFunctionPS(float *C) {
  D3D9FFPixelState s = {};
  for (uint32_t i = 0; i < ff::MaxStages; i++) {
    auto &tss = texture_stage_states_[i];
    auto &stage = s.stages[i];
    stage.color_op = tss[D3DTSS_COLOROP];
    stage.color_arg[0] = tss[D3DTSS_COLORARG0];
    stage.color_arg[1] = tss[D3DTSS_COLORARG1];
    stage.color_arg[2] = tss[D3DTSS_COLORARG2];
    stage.alpha_op = tss[D3DTSS_ALPHAOP];
    stage.alpha_arg[0] = tss[D3DTSS_ALPHAARG0];
    stage.alpha_arg[1] = tss[D3DTSS_ALPHAARG1];
    stage.alpha_arg[2] = tss[D3DTSS_ALPHAARG2];
    stage.result_arg = tss[D3DTSS_RESULTARG];
    stage.texcoord = tss[D3DTSS_TEXCOORDINDEX] & 0xFFFF;
    if (tss[D3DTSS_TEXCOORDINDEX] & 0xFFFF0000) {
      WARN_ONCE("D3D9: generated texture coordinates are not implemented");
    }
    if (tss[D3DTSS_TEXTURETRANSFORMFLAGS] != D3DTTFF_DISABLE) {
      WARN_ONCE("D3D9: texture coordinate transforms are not implemented");
    }
    if (auto texture = textures_[i].texture)
      stage.texture_type = texture->GetType();
    StoreColor(C + 4 * (ff::PSStageConstantBase + i), (D3DCOLOR)tss[D3DTSS_CONSTANT]);
  }
  s.specular = render_states_[D3DRS_SPECULARENABLE];
  StoreColor(C + 4 * ff::PSTextureFactor, (D3DCOLOR)render_states_[D3DRS_TEXTUREFACTOR]);
  return FixedFunctionShader(D3D9GenerateFixedFunctionPS(s), true);
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) {
  auto lock = Lock();
  if (!pMatrix || (UINT)State >= transforms_.size())
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->SetTransform(State, *pMatrix);
    return D3D_OK;
  }
  transforms_[State] = *pMatrix;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) {
  auto lock = Lock();
  if (!pMatrix || (UINT)State >= transforms_.size())
    return D3DERR_INVALIDCALL;
  *pMatrix = transforms_[State];
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) {
  auto lock = Lock();
  if (!pMatrix || (UINT)State >= transforms_.size())
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->SetTransform(State, Multiply(*pMatrix, transforms_[State]));
    return D3D_OK;
  }
  transforms_[State] = Multiply(*pMatrix, transforms_[State]);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
  auto lock = Lock();
  if (Stage >= texture_stage_states_.size() || Type > D3DTSS_CONSTANT)
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->SetTextureStageState(Stage, Type, Value);
    return D3D_OK;
  }
  texture_stage_states_[Stage][Type] = Value;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) {
  auto lock = Lock();
  if (!pValue || Stage >= texture_stage_states_.size() || Type > D3DTSS_CONSTANT)
    return D3DERR_INVALIDCALL;
  *pValue = texture_stage_states_[Stage][Type];
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetMaterial(const D3DMATERIAL9 *pMaterial) {
  auto lock = Lock();
  if (!pMaterial)
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->SetMaterial(*pMaterial);
    return D3D_OK;
  }
  material_ = *pMaterial;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetMaterial(D3DMATERIAL9 *pMaterial) {
  auto lock = Lock();
  if (!pMaterial)
    return D3DERR_INVALIDCALL;
  *pMaterial = material_;
  return D3D_OK;
}

static constexpr DWORD kMaxLightIndex = 4096;

static D3DLIGHT9
DefaultLight() {
  D3DLIGHT9 light = {};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1.0f, 1.0f, 1.0f, 0.0f};
  light.Direction = {0.0f, 0.0f, 1.0f};
  return light;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::SetLight(DWORD Index, const D3DLIGHT9 *pLight) {
  auto lock = Lock();
  if (!pLight || Index >= kMaxLightIndex)
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->SetLight(Index, *pLight);
    return D3D_OK;
  }
  if (Index >= lights_.size())
    lights_.resize(Index + 1, {DefaultLight(), false});
  lights_[Index].first = *pLight;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetLight(DWORD Index, D3DLIGHT9 *pLight) {
  auto lock = Lock();
  if (!pLight || Index >= lights_.size())
    return D3DERR_INVALIDCALL;
  *pLight = lights_[Index].first;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::LightEnable(DWORD Index, BOOL Enable) {
  auto lock = Lock();
  if (Index >= kMaxLightIndex)
    return D3DERR_INVALIDCALL;
  if (recording_) {
    recording_->LightEnable(Index, Enable);
    return D3D_OK;
  }
  if (Index >= lights_.size())
    lights_.resize(Index + 1, {DefaultLight(), false});
  lights_[Index].second = Enable;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::GetLightEnable(DWORD Index, BOOL *pEnable) {
  auto lock = Lock();
  if (!pEnable || Index >= lights_.size())
    return D3DERR_INVALIDCALL;
  // native d3d9 reports enabled lights as 128
  *pEnable = lights_[Index].second ? 128 : 0;
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB) {
  auto lock = Lock();
  InitReturnPtr(ppSB);
  if (!ppSB || (Type != D3DSBT_ALL && Type != D3DSBT_PIXELSTATE && Type != D3DSBT_VERTEXSTATE) || recording_)
    return D3DERR_INVALIDCALL;
  *ppSB = ref(new MTLD3D9StateBlock(this, Type));
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::BeginStateBlock() {
  auto lock = Lock();
  if (recording_)
    return D3DERR_INVALIDCALL;
  recording_ = new MTLD3D9StateBlock(this);
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D9Device::EndStateBlock(IDirect3DStateBlock9 **ppSB) {
  auto lock = Lock();
  InitReturnPtr(ppSB);
  if (!ppSB || !recording_)
    return D3DERR_INVALIDCALL;
  *ppSB = ref(recording_.ptr());
  recording_ = nullptr;
  return D3D_OK;
}

} // namespace dxmt
