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

#include "d3d9_fixed_function.hpp"
#include "d3d9_bytecode.hpp"
#include "log/log.hpp"
#include <bit>
#include <initializer_list>

namespace dxmt {

using namespace d3d9bc;

namespace {

constexpr DWORD kXXXX = 0x00, kYYYY = 0x55, kZZZZ = 0xAA, kWWWW = 0xFF, kXYZW = 0xE4;
constexpr uint32_t kHelperConst = 15; // (0, 1, 0.5, 2)

DWORD
Reg(RegisterType Type, uint32_t N) {
  uint32_t t = (uint32_t)Type;
  return 0x80000000u | N | ((t & 7) << 28) | ((t & 0x18) << 8);
}

DWORD
Dst(RegisterType Type, uint32_t N, uint32_t Mask = 0xF, bool Saturate = false) {
  return Reg(Type, N) | (Mask << 16) | (Saturate ? kResultSaturate << 20 : 0);
}

DWORD
Src(RegisterType Type, uint32_t N, DWORD Swizzle = kXYZW, SrcModifier Mod = SrcModifier::None) {
  return Reg(Type, N) | (Swizzle << 16) | ((DWORD)Mod << 24);
}

DWORD
Swizzled(DWORD Token, DWORD Swizzle) {
  return (Token & ~0x00FF0000u) | (Swizzle << 16);
}

DWORD
Modified(DWORD Token, SrcModifier Mod) {
  return (Token & ~0x0F000000u) | ((DWORD)Mod << 24);
}

DWORD
Temp(uint32_t N, DWORD Swizzle = kXYZW) {
  return Src(RegisterType::Temp, N, Swizzle);
}

DWORD
Helper(DWORD Swizzle, SrcModifier Mod = SrcModifier::None) {
  return Src(RegisterType::Const, kHelperConst, Swizzle, Mod);
}

struct Writer {
  std::vector<DWORD> code;

  void
  op(Opcode Op, std::initializer_list<DWORD> Args) {
    code.push_back((DWORD)Op | ((DWORD)Args.size() << 24));
    code.insert(code.end(), Args);
  }

  void
  dcl(Usage U, uint32_t Index, DWORD DstToken) {
    op(Opcode::Dcl, {0x80000000u | (DWORD)U | (Index << 16), DstToken});
  }

  void
  dcl_sampler(TextureType Type, uint32_t N) {
    op(Opcode::Dcl, {0x80000000u | ((DWORD)Type << 27), Dst(RegisterType::Sampler, N)});
  }

  void
  def_helper() {
    op(Opcode::Def,
       {Dst(RegisterType::Const, kHelperConst), std::bit_cast<DWORD>(0.0f), std::bit_cast<DWORD>(1.0f),
        std::bit_cast<DWORD>(0.5f), std::bit_cast<DWORD>(2.0f)});
  }

  // Copies a modified source into Scratch so another modifier can be applied to it.
  DWORD
  plain(DWORD Token, uint32_t Scratch) {
    if (src_modifier(Token) == SrcModifier::None)
      return Token;
    op(Opcode::Mov, {Dst(RegisterType::Temp, Scratch), Token});
    return Temp(Scratch);
  }
};

DWORD
MaterialSource(D3DMATERIALCOLORSOURCE Source, uint32_t MaterialConst) {
  if (Source == D3DMCS_COLOR1)
    return Src(RegisterType::Input, 2);
  if (Source == D3DMCS_COLOR2)
    return Src(RegisterType::Input, 3);
  return Src(RegisterType::Const, MaterialConst);
}

} // namespace

std::vector<DWORD>
D3D9GenerateFixedFunctionVS(const D3D9FFVertexState &S) {
  Writer w;
  w.code.push_back(0xFFFE0300);
  auto in = [](uint32_t n) { return Src(RegisterType::Input, n); };
  auto out = [](uint32_t n, uint32_t mask = 0xF, bool sat = false) { return Dst(RegisterType::Output, n, mask, sat); };
  auto c = [](uint32_t n, DWORD swizzle = kXYZW) { return Src(RegisterType::Const, n, swizzle); };

  w.dcl(S.pretransformed ? Usage::PositionT : Usage::Position, 0, Dst(RegisterType::Input, 0));
  if (S.normal)
    w.dcl(Usage::Normal, 0, Dst(RegisterType::Input, 1));
  if (S.diffuse)
    w.dcl(Usage::Color, 0, Dst(RegisterType::Input, 2));
  if (S.specular)
    w.dcl(Usage::Color, 1, Dst(RegisterType::Input, 3));
  for (uint32_t i = 0; i < 8; i++)
    if (S.texcoord_mask & (1 << i))
      w.dcl(Usage::TexCoord, i, Dst(RegisterType::Input, 4 + i));
  w.dcl(Usage::Position, 0, out(0));
  w.dcl(Usage::Color, 0, out(1));
  w.dcl(Usage::Color, 1, out(2));
  for (uint32_t i = 0; i < 8; i++)
    if (S.texcoord_mask & (1 << i))
      w.dcl(Usage::TexCoord, i, out(3 + i));
  w.def_helper();

  if (S.pretransformed) {
    w.op(Opcode::Mad, {Dst(RegisterType::Temp, 0, 0x7), in(0), c(ff::VSViewportScale), c(ff::VSViewportOffset)});
    w.op(Opcode::Rcp, {Dst(RegisterType::Temp, 0, 0x8), Src(RegisterType::Input, 0, kWWWW)});
    w.op(Opcode::Mul, {out(0, 0x7), Temp(0), Temp(0, kWWWW)});
    w.op(Opcode::Mov, {out(0, 0x8), Temp(0, kWWWW)});
  } else {
    for (uint32_t i = 0; i < 4; i++)
      w.op(Opcode::Dp4, {out(0, 1 << i), in(0), c(ff::VSWorldViewProj + i)});
  }

  if (S.lighting && !S.pretransformed) {
    DWORD diffuse = MaterialSource(S.diffuse_source, ff::VSMaterialDiffuse);
    DWORD ambient = MaterialSource(S.ambient_source, ff::VSMaterialAmbient);
    DWORD emissive = MaterialSource(S.emissive_source, ff::VSMaterialEmissive);
    w.op(Opcode::Mad, {Dst(RegisterType::Temp, 1), c(ff::VSAmbient), ambient, emissive});
    if (S.normal && S.light_count) {
      for (uint32_t i = 0; i < 3; i++)
        w.op(Opcode::Dp3, {Dst(RegisterType::Temp, 2, 1 << i), in(1), c(ff::VSWorld + i)});
      w.op(Opcode::Nrm, {Dst(RegisterType::Temp, 3, 0x7), Temp(2)});
      w.op(Opcode::Mov, {Dst(RegisterType::Temp, 4), Helper(kXXXX)});
      for (uint32_t i = 0; i < S.light_count; i++) {
        w.op(Opcode::Dp3, {Dst(RegisterType::Temp, 5, 0x1), Temp(3), c(ff::VSLightBase + 2 * i)});
        w.op(Opcode::Max, {Dst(RegisterType::Temp, 5, 0x1), Temp(5), Helper(kXXXX)});
        w.op(Opcode::Mad, {Dst(RegisterType::Temp, 4, 0x7), c(ff::VSLightBase + 2 * i + 1), Temp(5, kXXXX), Temp(4)});
      }
      w.op(Opcode::Mad, {out(1, 0x7, true), Temp(4), diffuse, Temp(1)});
    } else {
      w.op(Opcode::Mov, {out(1, 0x7, true), Temp(1)});
    }
    w.op(Opcode::Mov, {out(1, 0x8), Swizzled(diffuse, kWWWW)});
    w.op(Opcode::Mov, {out(2), Helper(kXXXX)});
  } else {
    w.op(Opcode::Mov, {out(1), S.diffuse ? in(2) : Helper(kYYYY)});
    w.op(Opcode::Mov, {out(2), S.specular ? in(3) : Helper(kXXXX)});
  }

  for (uint32_t i = 0; i < 8; i++)
    if (S.texcoord_mask & (1 << i))
      w.op(Opcode::Mov, {out(3 + i), in(4 + i)});
  w.code.push_back(0x0000FFFF);
  return std::move(w.code);
}

namespace {

DWORD
StageArg(DWORD Arg, uint32_t Stage) {
  DWORD token;
  switch (Arg & D3DTA_SELECTMASK) {
  case D3DTA_DIFFUSE:
    token = Src(RegisterType::Input, 0);
    break;
  case D3DTA_TEXTURE:
    token = Temp(1);
    break;
  case D3DTA_TFACTOR:
    token = Src(RegisterType::Const, ff::PSTextureFactor);
    break;
  case D3DTA_SPECULAR:
    token = Src(RegisterType::Input, 1);
    break;
  case D3DTA_TEMP:
    token = Temp(2);
    break;
  case D3DTA_CONSTANT:
    token = Src(RegisterType::Const, ff::PSStageConstantBase + Stage);
    break;
  default:
    token = Temp(0);
    break;
  }
  if (Arg & D3DTA_ALPHAREPLICATE)
    token = Swizzled(token, kWWWW);
  if (Arg & D3DTA_COMPLEMENT)
    token = Modified(token, SrcModifier::Comp);
  return token;
}

bool
UsesTexture(DWORD Op, const DWORD *Args) {
  if (Op == D3DTOP_BLENDTEXTUREALPHA || Op == D3DTOP_BLENDTEXTUREALPHAPM)
    return true;
  for (uint32_t i = 0; i < 3; i++)
    if ((Args[i] & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
      return true;
  return false;
}

void
EmitStageOp(Writer &w, DWORD Op, const DWORD *Args, uint32_t Stage, uint32_t Mask) {
  DWORD a0 = StageArg(Args[0], Stage), a1 = StageArg(Args[1], Stage), a2 = StageArg(Args[2], Stage);
  DWORD d = Dst(RegisterType::Temp, 5, Mask, true);
  DWORD scratch = Dst(RegisterType::Temp, 3);
  switch (Op) {
  case D3DTOP_SELECTARG1:
    return w.op(Opcode::Mov, {d, a1});
  case D3DTOP_SELECTARG2:
    return w.op(Opcode::Mov, {d, a2});
  case D3DTOP_MODULATE:
    return w.op(Opcode::Mul, {d, a1, a2});
  case D3DTOP_MODULATE2X:
    w.op(Opcode::Mul, {scratch, a1, a2});
    return w.op(Opcode::Add, {d, Temp(3), Temp(3)});
  case D3DTOP_MODULATE4X:
    w.op(Opcode::Mul, {scratch, a1, a2});
    w.op(Opcode::Add, {scratch, Temp(3), Temp(3)});
    return w.op(Opcode::Add, {d, Temp(3), Temp(3)});
  case D3DTOP_ADD:
    return w.op(Opcode::Add, {d, a1, a2});
  case D3DTOP_ADDSIGNED:
    w.op(Opcode::Add, {scratch, a1, a2});
    return w.op(Opcode::Add, {d, Temp(3), Helper(kZZZZ, SrcModifier::Neg)});
  case D3DTOP_ADDSIGNED2X:
    w.op(Opcode::Add, {scratch, a1, a2});
    w.op(Opcode::Add, {scratch, Temp(3), Helper(kZZZZ, SrcModifier::Neg)});
    return w.op(Opcode::Add, {d, Temp(3), Temp(3)});
  case D3DTOP_SUBTRACT:
    return w.op(Opcode::Add, {d, a1, Modified(w.plain(a2, 4), SrcModifier::Neg)});
  case D3DTOP_ADDSMOOTH:
    return w.op(Opcode::Mad, {d, Modified(w.plain(a1, 4), SrcModifier::Comp), a2, a1});
  case D3DTOP_BLENDDIFFUSEALPHA:
    return w.op(Opcode::Lrp, {d, Src(RegisterType::Input, 0, kWWWW), a1, a2});
  case D3DTOP_BLENDTEXTUREALPHA:
    return w.op(Opcode::Lrp, {d, Temp(1, kWWWW), a1, a2});
  case D3DTOP_BLENDFACTORALPHA:
    return w.op(Opcode::Lrp, {d, Src(RegisterType::Const, ff::PSTextureFactor, kWWWW), a1, a2});
  case D3DTOP_BLENDCURRENTALPHA:
    return w.op(Opcode::Lrp, {d, Temp(0, kWWWW), a1, a2});
  case D3DTOP_BLENDTEXTUREALPHAPM:
    return w.op(Opcode::Mad, {d, a2, Src(RegisterType::Temp, 1, kWWWW, SrcModifier::Comp), a1});
  case D3DTOP_MODULATEALPHA_ADDCOLOR:
    return w.op(Opcode::Mad, {d, Swizzled(a1, kWWWW), a2, a1});
  case D3DTOP_MODULATECOLOR_ADDALPHA:
    return w.op(Opcode::Mad, {d, a1, a2, Swizzled(a1, kWWWW)});
  case D3DTOP_MODULATEINVALPHA_ADDCOLOR:
    return w.op(Opcode::Mad, {d, Modified(Swizzled(w.plain(a1, 4), kWWWW), SrcModifier::Comp), a2, a1});
  case D3DTOP_MODULATEINVCOLOR_ADDALPHA:
    return w.op(Opcode::Mad, {d, Modified(w.plain(a1, 4), SrcModifier::Comp), a2, Swizzled(a1, kWWWW)});
  case D3DTOP_DOTPRODUCT3:
    w.op(Opcode::Mad, {scratch, a1, Helper(kWWWW), Helper(kYYYY, SrcModifier::Neg)});
    w.op(Opcode::Mad, {Dst(RegisterType::Temp, 4), a2, Helper(kWWWW), Helper(kYYYY, SrcModifier::Neg)});
    return w.op(Opcode::Dp3, {Dst(RegisterType::Temp, 5, 0xF, true), Temp(3), Temp(4)});
  case D3DTOP_MULTIPLYADD:
    return w.op(Opcode::Mad, {d, a1, a2, a0});
  case D3DTOP_LERP:
    return w.op(Opcode::Lrp, {d, a0, a1, a2});
  default: {
    WARN_ONCE("D3D9: unsupported fixed-function texture op ", Op);
    return w.op(Opcode::Mov, {d, Temp(0)});
  }
  }
}

TextureType
SamplerType(D3DRESOURCETYPE Type) {
  return Type == D3DRTYPE_CUBETEXTURE ? TextureType::Cube : Type == D3DRTYPE_VOLUMETEXTURE ? TextureType::Volume
                                                                                          : TextureType::Texture2D;
}

} // namespace

std::vector<DWORD>
D3D9GenerateFixedFunctionPS(const D3D9FFPixelState &S) {
  Writer w;
  w.code.push_back(0xFFFF0300);

  uint32_t stage_count = 0, texcoord_mask = 0;
  for (; stage_count < ff::MaxStages; stage_count++) {
    auto &stage = S.stages[stage_count];
    if (stage.color_op == D3DTOP_DISABLE)
      break;
    if (stage.texture_type)
      texcoord_mask |= 1 << (stage.texcoord & 7);
  }

  w.dcl(Usage::Color, 0, Dst(RegisterType::Input, 0));
  w.dcl(Usage::Color, 1, Dst(RegisterType::Input, 1));
  for (uint32_t i = 0; i < 8; i++)
    if (texcoord_mask & (1 << i))
      w.dcl(Usage::TexCoord, i, Dst(RegisterType::Input, 2 + i));
  for (uint32_t i = 0; i < stage_count; i++)
    if (S.stages[i].texture_type)
      w.dcl_sampler(SamplerType(S.stages[i].texture_type), i);
  w.def_helper();

  w.op(Opcode::Mov, {Dst(RegisterType::Temp, 0), Src(RegisterType::Input, 0)});
  w.op(Opcode::Mov, {Dst(RegisterType::Temp, 2), Helper(kXXXX)});
  for (uint32_t i = 0; i < stage_count; i++) {
    auto &stage = S.stages[i];
    bool alpha_enabled = stage.alpha_op != D3DTOP_DISABLE;
    bool textured = UsesTexture(stage.color_op, stage.color_arg) ||
                    (alpha_enabled && UsesTexture(stage.alpha_op, stage.alpha_arg));
    // Like wined3d, a stage that reads an unbound texture passes the current color through.
    if (textured && !stage.texture_type)
      continue;
    if (textured)
      w.op(Opcode::Tex, {Dst(RegisterType::Temp, 1), Src(RegisterType::Input, 2 + (stage.texcoord & 7)),
                         Src(RegisterType::Sampler, i)});
    EmitStageOp(w, stage.color_op, stage.color_arg, i, 0x7);
    if (stage.color_op != D3DTOP_DOTPRODUCT3) {
      if (alpha_enabled)
        EmitStageOp(w, stage.alpha_op, stage.alpha_arg, i, 0x8);
      else
        w.op(Opcode::Mov, {Dst(RegisterType::Temp, 5, 0x8), Temp(0)});
    }
    w.op(Opcode::Mov, {Dst(RegisterType::Temp, stage.result_arg == D3DTA_TEMP ? 2u : 0u), Temp(5)});
  }
  if (S.specular)
    w.op(Opcode::Add, {Dst(RegisterType::Temp, 0, 0x7, true), Temp(0), Src(RegisterType::Input, 1)});
  w.op(Opcode::Mov, {Dst(RegisterType::ColorOut, 0), Temp(0)});
  w.code.push_back(0x0000FFFF);
  return std::move(w.code);
}

} // namespace dxmt
