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

#include "d3d9_interface.hpp"
#include "log/log.hpp"

namespace dxmt {

Logger Logger::s_instance("d3d9.log");

extern "C" IDirect3D9 *WINAPI
Direct3DCreate9(UINT SDKVersion) {
  IDirect3D9Ex *d3d9 = nullptr;
  if (FAILED(CreateD3D9(false, &d3d9)))
    return nullptr;
  return d3d9;
}

extern "C" HRESULT WINAPI
Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D9Ex) {
  if (!ppD3D9Ex)
    return D3DERR_INVALIDCALL;
  *ppD3D9Ex = nullptr;
  return CreateD3D9(true, ppD3D9Ex);
}

extern "C" void *WINAPI
Direct3DShaderValidatorCreate9() {
  ERR("Direct3DShaderValidatorCreate9: not implemented");
  return nullptr;
}

extern "C" void WINAPI
PSGPError(void *, UINT, UINT) {}

extern "C" void WINAPI
PSGPSampleTexture(void *, UINT, float (*)[4], UINT, float (*)[4]) {}

extern "C" int WINAPI
D3DPERF_BeginEvent(D3DCOLOR, LPCWSTR) {
  return 0;
}

extern "C" int WINAPI
D3DPERF_EndEvent() {
  return 0;
}

extern "C" DWORD WINAPI
D3DPERF_GetStatus() {
  return 0;
}

extern "C" BOOL WINAPI
D3DPERF_QueryRepeatFrame() {
  return FALSE;
}

extern "C" void WINAPI
D3DPERF_SetMarker(D3DCOLOR, LPCWSTR) {}

extern "C" void WINAPI
D3DPERF_SetOptions(DWORD) {}

extern "C" void WINAPI
D3DPERF_SetRegion(D3DCOLOR, LPCWSTR) {}

extern "C" void WINAPI
DebugSetLevel() {}

extern "C" int WINAPI
DebugSetMute() {
  return 0;
}

extern "C" BOOL WINAPI
Direct3D9EnableMaximizedWindowedModeShim(BOOL) {
  return FALSE;
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  return TRUE;
}

} // namespace dxmt
