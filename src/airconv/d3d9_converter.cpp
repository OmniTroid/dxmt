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

#include "d3d9_decoder.hpp"
#include "dxbc_converter.hpp"
#include "llvm/Support/raw_ostream.h"

namespace dxmt::d3d9 {

using namespace dxbc;
using namespace air;
using d3d9bc::Usage;

namespace {

struct Driver {
  SM50ShaderInternal *shader;
  ShaderData &data;
  llvm::LLVMContext &context;
  llvm::Module &module;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *args;

  FunctionSignatureBuilder func_signature;
  Prologues prologue;
  Epilogues epilogue;
  io_binding_map resource_map;
  AirType types;
  SM50_SHADER_METAL_VERSION metal_version = SM50_SHADER_METAL_310;
  SM50_SHADER_FLAG shader_flags = {};
  uint32_t max_output_register;

  Driver(
      SM50ShaderInternal *shader, llvm::LLVMContext &context, llvm::Module &module,
      SM50_SHADER_COMPILATION_ARGUMENT_DATA *args
  ) :
      shader(shader),
      data(*shader->d3d9),
      context(context),
      module(module),
      args(args),
      func_signature(shader->func_signature),
      types(context),
      max_output_register(shader->max_output_register) {
    SM50_SHADER_COMMON_DATA *common = nullptr;
    if (args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(args, &common)) {
      metal_version = common->metal_version;
      shader_flags = common->flags;
    }
  }

  void
  defineVertexInputs() {
    SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout = nullptr;
    args_get_data<SM50_SHADER_IA_INPUT_LAYOUT, SM50_SHADER_IA_INPUT_LAYOUT_DATA>(args, &ia_layout);
    uint32_t vbuf_table = ~0u;
    for (auto &input : data.inputs) {
      const SM50_IA_INPUT_ELEMENT *element = nullptr;
      for (unsigned i = 0; ia_layout && i < ia_layout->num_elements; i++)
        if (ia_layout->elements[i].reg == input.reg)
          element = &ia_layout->elements[i];
      uint32_t reg = input.reg;
      if (element) {
        if (vbuf_table == ~0u)
          vbuf_table = func_signature.DefineInput(ArgumentBindingBuffer{
              .buffer_size = {},
              .location_index = SM50_BINDING_INDEX_VERTEX_BUFFER,
              .array_size = 0,
              .memory_access = MemoryAccess::read,
              .address_space = AddressSpace::constant,
              .type = msl_uint,
              .arg_name = "vertex_buffers",
              .raster_order_group = {},
          });
        auto info = *element;
        uint32_t fixup = info.format >> 16;
        info.format &= 0xFFFF;
        auto slot_mask = ia_layout->slot_mask;
        prologue.Add([=](IOHelper &io) -> llvm::Error {
          if (auto err = io.PullVertexInput(vbuf_table, reg, 0xF, info, slot_mask))
            return err;
          return io.FixupVertexInput(reg, fixup);
        });
      } else if (!ia_layout) {
        auto index = func_signature.DefineInput(InputVertexStageIn{
            .attribute = reg, .type = InputAttributeComponentType::Float, .name = input.attributeName()
        });
        prologue.Add([=](IOHelper &io) -> llvm::Error { return io.InitializeInput(index, reg, 0xF); });
      }
    }
  }

  void
  defineVertexOutputs() {
    uint32_t defined = 0;
    auto has = [&](Usage usage, uint32_t index) {
      for (auto &out : data.outputs)
        if (out.usage == usage && out.usage_index == index)
          return true;
      return false;
    };

    for (auto &out : data.outputs) {
      uint32_t reg = out.reg;
      if (out.usage == Usage::Position && out.usage_index == 0) {
        auto index = func_signature.DefineOutput(OutputPosition{.type = msl_float4});
        epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
          return io.PopulateOutputSanitizePosition(reg, 0xF, ret, index);
        });
        continue;
      }
      if (out.usage == Usage::PSize)
        continue;
      auto index = func_signature.DefineOutput(OutputVertex{.user = out.attributeName(), .type = msl_float4});
      epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
        return io.PopulateOutput(reg, 0xF, ret, index);
      });
      defined++;
    }

    // Pixel shaders before ps_3_0 read colors and texture coordinates the vertex shader may not write; Metal
    // refuses to link a fragment input with no matching vertex output, so emit zeros for the missing ones.
    uint32_t fill_reg = shader->max_output_register;
    auto fill = [&](Usage usage, uint32_t usage_index) {
      if (has(usage, usage_index))
        return;
      IOElement element{fill_reg++, usage, usage_index, 0xF, false};
      uint32_t reg = element.reg;
      auto index = func_signature.DefineOutput(OutputVertex{.user = element.attributeName(), .type = msl_float4});
      epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
        return io.PopulateOutput(reg, 0xF, ret, index);
      });
    };
    for (uint32_t i = 0; i < 2; i++)
      fill(Usage::Color, i);
    for (uint32_t i = 0; i < 8; i++)
      fill(Usage::TexCoord, i);
    fill(Usage::Fog, 0);
    max_output_register = fill_reg;
  }

  void
  definePixelInputs() {
    for (auto &input : data.inputs) {
      uint32_t reg = input.reg;
      auto index = func_signature.DefineInput(InputFragmentStageIn{
          .user = input.attributeName(),
          .type = msl_float4,
          .interpolation = input.centroid ? Interpolation::centroid_perspective : Interpolation::center_perspective,
          .pull_mode = false,
      });
      prologue.Add([=](IOHelper &io) -> llvm::Error { return io.InitializeInput(index, reg, 0xF); });
    }
    if (data.uses_vpos) {
      auto index = func_signature.DefineInput(InputPosition{.interpolation = Interpolation::center_no_perspective});
      prologue.Add([=](IOHelper &io) -> llvm::Error { return io.InitializeInput(index, reg::PSInputVPos, 0xF); });
    }
    if (data.uses_vface) {
      auto index = func_signature.DefineInput(InputFrontFacing{});
      prologue.Add([=](IOHelper &io) -> llvm::Error { return io.InitializeInput(index, reg::PSInputVFace, 0x1); });
    }
  }

  void
  definePixelOutputs() {
    SM50_SHADER_PSO_PIXEL_SHADER_DATA *pso = nullptr;
    args_get_data<SM50_SHADER_PSO_PIXEL_SHADER, SM50_SHADER_PSO_PIXEL_SHADER_DATA>(args, &pso);

    for (uint32_t rt = 0; rt < 4; rt++) {
      if (!(data.color_output_mask & (1 << rt)))
        continue;
      auto type = pso ? component_type_from_pixel_format((MTLPixelFormat)pso->pixel_formats[rt])
                      : RegisterComponentType::Unknown;
      if (type == RegisterComponentType::Unknown)
        type = RegisterComponentType::Float;
      auto index = func_signature.DefineOutput(
          OutputRenderTarget{.dual_source_blending = false, .index = rt, .type = to_msl_type(type)}
      );
      bool fix_unorm = pso && type == RegisterComponentType::Float && (pso->unorm_output_reg_mask & (1 << rt));
      epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
        return fix_unorm ? io.PopulateOutputFixUnorm(rt, 0xF, ret, index) : io.PopulateOutput(rt, 0xF, ret, index);
      });
    }

    if (data.writes_depth) {
      prologue.Add([](IOHelper &io) -> llvm::Error { return io.CreateDepthOutputRegister(); });
      if (!pso || !pso->disable_depth_output) {
        auto index = func_signature.DefineOutput(OutputDepth{.depth_argument = DepthArgument::any});
        epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
          return io.PopulateOutputDepth(ret, index);
        });
      }
    }

    if (pso && pso->sample_mask != 0xffffffff) {
      auto sample_mask = pso->sample_mask;
      auto index = func_signature.DefineOutput(OutputCoverageMask{});
      epilogue.Add([=](llvm::Value *ret, IOHelper &io) -> llvm::Expected<llvm::Value *> {
        return io.PopulateDefaultCoverageMask(ret, index, sample_mask);
      });
    }
  }

  llvm::Error
  run(const char *name) {
    bool pixel = data.version.pixel;
    uint32_t vertex_idx = 0, base_vertex_idx = 0, instance_idx = 0, base_instance_idx = 0;
    if (pixel) {
      definePixelInputs();
      definePixelOutputs();
    } else {
      defineVertexInputs();
      defineVertexOutputs();
      vertex_idx = func_signature.DefineInput(InputVertexID{});
      base_vertex_idx = func_signature.DefineInput(InputBaseVertex{});
      instance_idx = func_signature.DefineInput(InputInstanceID{});
      base_instance_idx = func_signature.DefineInput(InputBaseInstance{});
    }

    auto binding_map = setup_binding_table2(&shader->shader_info, func_signature, module);
    auto [function, function_metadata] = func_signature.CreateFunction(name, context, module, 0, false);

    auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
    auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
    llvm::IRBuilder<> builder(entry_bb);
    llvm::raw_null_ostream nulldbg{};
    llvm::air::AIRBuilder air(
        {
            .sampleNaNToZero = bool(shader_flags & SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO),
            .defuseFma = bool(shader_flags & SM50_SHADER_FLAG_DEFUSE_FMA),
        },
        builder, nulldbg
    );
    setup_metal_version(module, metal_version);

    if (!pixel) {
      resource_map.vertex_id_with_base = function->getArg(vertex_idx);
      resource_map.base_vertex_id = function->getArg(base_vertex_idx);
      resource_map.instance_id_with_base = function->getArg(instance_idx);
      resource_map.base_instance_id = function->getArg(base_instance_idx);
      resource_map.vertex_id = builder.CreateSub(resource_map.vertex_id_with_base, resource_map.base_vertex_id);
      resource_map.instance_id = builder.CreateSub(resource_map.instance_id_with_base, resource_map.base_instance_id);
    }

    uint32_t max_input_register = shader->max_input_register;
    auto input_ty = llvm::ArrayType::get(types._int4, max_input_register);
    resource_map.input.ptr_int4 = builder.CreateAlloca(input_ty);
    resource_map.input.ptr_float4 = builder.CreateBitCast(
        resource_map.input.ptr_int4, llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
    );
    builder.CreateStore(llvm::ConstantAggregateZero::get(input_ty), resource_map.input.ptr_int4);
    resource_map.input_element_count = max_input_register;

    auto output_ty = llvm::ArrayType::get(types._int4, max_output_register);
    resource_map.output.ptr_int4 = builder.CreateAlloca(output_ty);
    resource_map.output.ptr_float4 = builder.CreateBitCast(
        resource_map.output.ptr_int4, llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
    );
    builder.CreateStore(llvm::ConstantAggregateZero::get(output_ty), resource_map.output.ptr_int4);
    resource_map.output_element_count = max_output_register;

    setup_temp_register(&shader->shader_info, resource_map, types, module, builder);
    setup_immediate_constant_buffer(&shader->shader_info, resource_map, types, module, builder);

    struct context ctx{
        .builder = builder,
        .air = air,
        .binding = *binding_map,
        .llvm = context,
        .module = module,
        .function = function,
        .resource = resource_map,
        .types = types,
        .pso_sample_mask = 0xffffffff,
        .shader_type = shader->shader_type,
        .metal_version = metal_version,
    };

    IOHelper io(air, function, types, resource_map);
    if (auto err = prologue.Run(io))
      return err;
    auto real_entry = convert_basicblocks(shader->entry(), ctx, epilogue_bb);
    if (auto err = real_entry.takeError())
      return err;
    builder.CreateBr(real_entry.get());

    builder.SetInsertPoint(epilogue_bb);
    auto result = epilogue.Run(function->getReturnType(), io);
    if (auto err = result.takeError())
      return err;
    if (result.get())
      builder.CreateRet(result.get());
    else
      builder.CreateRetVoid();

    module.getOrInsertNamedMetadata(pixel ? "air.fragment" : "air.vertex")->addOperand(function_metadata);
    return llvm::Error::success();
  }
};

} // namespace

llvm::Error
convert_shader(
    SM50ShaderInternal *pShader, const char *name, llvm::LLVMContext &context, llvm::Module &module,
    SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  Driver driver(pShader, context, module, pArgs);
  return driver.run(name);
}

} // namespace dxmt::d3d9

using namespace dxmt;

AIRCONV_API int
SM30Initialize(
    const void *pBytecode, size_t BytecodeSize, sm50_shader_t *ppShader, MTL_SHADER_REFLECTION *pRefl,
    sm50_error_t *ppError
) {
  using namespace dxmt::dxbc;
  if (ppError)
    *ppError = nullptr;
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);

  d3d9bc::Version version;
  if (!ppShader || !pBytecode || BytecodeSize < 8 || !d3d9bc::read_version(*(const uint32_t *)pBytecode, version)) {
    errorOut << "Invalid D3D9 shader bytecode";
    SM50_RETURN_ERROR(errorObj, ppError, 1);
  }

  auto shader = std::make_unique<SM50ShaderInternal>();
  shader->shader_type = version.pixel ? microsoft::D3D10_SB_PIXEL_SHADER : microsoft::D3D10_SB_VERTEX_SHADER;
  shader->d3d9 = std::make_shared<d3d9::ShaderData>();

  if (auto err = d3d9::decode_shader((const uint32_t *)pBytecode, BytecodeSize / 4, *shader, *shader->d3d9)) {
    errorOut << *err;
    SM50_RETURN_ERROR(errorObj, ppError, 1);
  }

  if (version.pixel) {
    shader->max_input_register = d3d9::reg::PSInputCount;
    shader->max_output_register = 4;
    shader->pso_valid_output_reg_mask = shader->d3d9->color_output_mask;
  } else {
    shader->max_input_register = 16;
    shader->max_output_register = std::max(12u, d3d9::reg::VSOutLegacyCount);
  }

  auto masks = define_argument_bindings(shader.get());
  if (pRefl) {
    *pRefl = {};
    pRefl->ConstanttBufferTableBindIndex = shader->args_reflection_cbuffer.size() > 0 ? 29 : ~0u;
    pRefl->ArgumentBufferBindIndex = shader->args_reflection.size() > 0 ? 30 : ~0u;
    pRefl->ConstantBufferSlotMask = masks.cbuffer;
    pRefl->SamplerSlotMask = masks.sampler;
    pRefl->SRVSlotMaskHi = masks.srv_hi;
    pRefl->SRVSlotMaskLo = masks.srv_lo;
    pRefl->UAVSlotMask = masks.uav;
    pRefl->NumConstantBuffers = shader->args_reflection_cbuffer.size();
    pRefl->NumArguments = shader->args_reflection.size();
    if (version.pixel)
      pRefl->PixelShader.ValidRenderTargets = shader->pso_valid_output_reg_mask;
    pRefl->NumOutputElement = shader->max_output_register;
    pRefl->ArgumentTableQwords = shader->shader_info.binding_table.Size();
  }

  *ppShader = (sm50_shader_t)shader.release();
  return 0;
}
