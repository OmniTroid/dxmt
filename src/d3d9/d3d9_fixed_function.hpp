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

#include "d3d9.h"
#include <cstdint>
#include <vector>

namespace dxmt {

// Fixed-function state is compiled by generating vs_3_0/ps_3_0 bytecode that reads these constant registers.
namespace ff {

constexpr uint32_t VSWorldViewProj = 0;
constexpr uint32_t VSWorld = 4;
constexpr uint32_t VSViewportScale = 8;
constexpr uint32_t VSViewportOffset = 9;
constexpr uint32_t VSMaterialDiffuse = 10;
constexpr uint32_t VSMaterialAmbient = 11;
constexpr uint32_t VSMaterialSpecular = 12;
constexpr uint32_t VSMaterialEmissive = 13;
constexpr uint32_t VSAmbient = 14;
constexpr uint32_t VSLightBase = 16;
constexpr uint32_t VSConstantCount = VSLightBase + 2 * 8;

constexpr uint32_t PSTextureFactor = 0;
constexpr uint32_t PSStageConstantBase = 1;
constexpr uint32_t PSConstantCount = 16;

constexpr uint32_t MaxStages = 8;
constexpr uint32_t MaxLights = 8;

} // namespace ff

struct D3D9FFVertexState {
  bool pretransformed;
  bool normal;
  bool diffuse;
  bool specular;
  uint32_t texcoord_mask;
  bool lighting;
  uint32_t light_count;
  D3DMATERIALCOLORSOURCE diffuse_source;
  D3DMATERIALCOLORSOURCE ambient_source;
  D3DMATERIALCOLORSOURCE emissive_source;
};

struct D3D9FFStage {
  DWORD color_op;
  DWORD color_arg[3];
  DWORD alpha_op;
  DWORD alpha_arg[3];
  DWORD result_arg;
  DWORD texcoord;
  D3DRESOURCETYPE texture_type;
};

struct D3D9FFPixelState {
  D3D9FFStage stages[ff::MaxStages];
  bool specular;
};

std::vector<DWORD> D3D9GenerateFixedFunctionVS(const D3D9FFVertexState &State);
std::vector<DWORD> D3D9GenerateFixedFunctionPS(const D3D9FFPixelState &State);

} // namespace dxmt
