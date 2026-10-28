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

#include "d3d9_shader.hpp"
#include <optional>
#include <string>

namespace dxmt::d3d9 {

// Fills shader.bbs, shader.shader_info and data from SM1-SM3 tokens. Returns an error message on failure.
// Ps1TextureTypes gives the sampler types ps_1_x leaves undeclared (SM30_SHADER_D3D9_DATA::ps1_texture_types).
std::optional<std::string> decode_shader(
    const uint32_t *tokens, size_t count, dxbc::SM50ShaderInternal &shader, ShaderData &data,
    uint32_t Ps1TextureTypes = 0
);

} // namespace dxmt::d3d9
