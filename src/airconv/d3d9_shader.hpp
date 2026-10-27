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

#include "d3d9_bytecode.hpp"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

struct SM50_SHADER_COMPILATION_ARGUMENT_DATA;

namespace dxmt::dxbc {
class SM50ShaderInternal;
}

namespace dxmt::d3d9 {

// Register numbers in the IR register files. Vertex and pixel shaders are compiled separately and link by
// semantic name, so these only need to be consistent within one shader.
namespace reg {
constexpr uint32_t TempCount = 32;
constexpr uint32_t A0 = 32;
constexpr uint32_t P0 = 33;
constexpr uint32_t ALBase = 34;
constexpr uint32_t LoopCounterBase = 38;
constexpr uint32_t MaxLoopDepth = 4;
constexpr uint32_t VPos = 42;
constexpr uint32_t VFace = 43;
constexpr uint32_t ScratchBase = 44;
constexpr uint32_t ScratchCount = 12;
constexpr uint32_t TotalTemps = ScratchBase + ScratchCount;

// pixel shader inputs before ps_3_0: v0-v1 are colors, t0-t7 are texture coordinates
constexpr uint32_t PSTexCoordBase = 2;
constexpr uint32_t PSInputVPos = 12;
constexpr uint32_t PSInputVFace = 13;
constexpr uint32_t PSInputCount = 14;

// vertex shader outputs before vs_3_0
constexpr uint32_t VSOutPos = 0;
constexpr uint32_t VSOutFog = 1;
constexpr uint32_t VSOutPSize = 2;
constexpr uint32_t VSOutColorBase = 3;
constexpr uint32_t VSOutTexCoordBase = 5;
constexpr uint32_t VSOutLegacyCount = 13;
} // namespace reg

namespace cb {
constexpr uint32_t Float = 0;
constexpr uint32_t Int = 1;
constexpr uint32_t Bool = 2;
} // namespace cb

struct IOElement {
  uint32_t reg;
  d3d9bc::Usage usage;
  uint32_t usage_index;
  uint32_t mask;
  bool centroid;

  std::string
  attributeName() const {
    return std::string("d3d9_") + d3d9bc::usage_name(usage) + std::to_string(usage_index);
  }
};

struct ShaderData {
  d3d9bc::Version version;
  std::vector<IOElement> inputs;
  std::vector<IOElement> outputs;
  uint32_t color_output_mask = 0;
  bool writes_depth = false;
  bool uses_vpos = false;
  bool uses_vface = false;
};

llvm::Error convert_shader(
    dxbc::SM50ShaderInternal *pShader, const char *name, llvm::LLVMContext &context, llvm::Module &module,
    SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);

} // namespace dxmt::d3d9
