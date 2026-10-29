#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static int failures = 0;
static IDirect3DDevice9 *device;

static void
check(const char *name, bool ok) {
  printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
  failures += !ok;
}

static DWORD
read(IDirect3DSurface9 *s, int x, int y) {
  D3DLOCKED_RECT lr;
  DWORD value = 0xDEADBEEF;
  if (SUCCEEDED(s->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
    value = ((DWORD *)((char *)lr.pBits + y * lr.Pitch))[x];
    s->UnlockRect();
  }
  return value;
}

static HWND
window(HINSTANCE instance, const wchar_t *title) {
  return CreateWindowW(L"d3d9_misc", title, WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr, instance, nullptr);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = L"d3d9_misc";
  RegisterClassW(&wc);
  HWND hwnd = window(instance, L"d3d9_misc");

  IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferWidth = 64;
  pp.BackBufferHeight = 64;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.hDeviceWindow = hwnd;
  if (!d3d || FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device))) {
    printf("FAIL: device creation\n");
    return 1;
  }

  DWORD passes = 0;
  check("ValidateDevice", device->ValidateDevice(&passes) == D3D_OK && passes == 1);
  D3DCLIPSTATUS9 status = {D3DCS_LEFT, D3DCS_ALL}, got_status = {};
  device->SetClipStatus(&status);
  device->GetClipStatus(&got_status);
  check("clip status round trip", got_status.ClipUnion == D3DCS_LEFT && got_status.ClipIntersection == D3DCS_ALL);
  PALETTEENTRY palette[256] = {}, got_palette[256] = {};
  palette[7] = {1, 2, 3, 4};
  UINT current = 0;
  check("unknown palette fails", device->SetCurrentTexturePalette(3) == D3DERR_INVALIDCALL);
  device->SetPaletteEntries(3, palette);
  device->SetCurrentTexturePalette(3);
  device->GetPaletteEntries(3, got_palette);
  device->GetCurrentTexturePalette(&current);
  check("palette round trip", current == 3 && got_palette[7].peGreen == 2);
  device->SetNPatchMode(2.5f);
  check("N-patch mode round trip", device->GetNPatchMode() == 2.5f);

  // GDI drawing through GetDC lands in the surface
  IDirect3DSurface9 *sys = nullptr;
  device->CreateOffscreenPlainSurface(16, 16, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
  HDC dc = nullptr;
  check("GetDC", SUCCEEDED(sys->GetDC(&dc)) && dc);
  D3DLOCKED_RECT lr;
  check("locking during GetDC fails", sys->LockRect(&lr, nullptr, 0) == D3DERR_INVALIDCALL);
  RECT rect = {4, 4, 8, 8};
  HBRUSH brush = CreateSolidBrush(RGB(0x10, 0x20, 0x30));
  FillRect(dc, &rect, brush);
  check("ReleaseDC", sys->ReleaseDC(dc) == D3D_OK);
  check("GDI fill is visible", (read(sys, 5, 5) & 0xFFFFFF) == 0x102030 && (read(sys, 1, 1) & 0xFFFFFF) == 0);

  IDirect3DSurface9 *rt = nullptr, *rt_sys = nullptr;
  device->CreateRenderTarget(16, 16, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr);
  device->CreateOffscreenPlainSurface(16, 16, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &rt_sys, nullptr);
  device->ColorFill(rt, nullptr, 0xFF000000);
  dc = nullptr;
  check("GetDC on a render target", SUCCEEDED(rt->GetDC(&dc)) && dc);
  FillRect(dc, &rect, brush);
  rt->ReleaseDC(dc);
  device->GetRenderTargetData(rt, rt_sys);
  check("GDI fill reaches the render target", (read(rt_sys, 5, 5) & 0xFFFFFF) == 0x102030);
  DeleteObject(brush);

  // the front buffer holds the last presented frame
  IDirect3DSurface9 *front = nullptr;
  device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &front, nullptr);
  device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0f, 0);
  device->Present(nullptr, nullptr, nullptr, nullptr);
  check("GetFrontBufferData", device->GetFrontBufferData(0, front) == D3D_OK);
  check("front buffer after the first present", (read(front, 10, 10) & 0xFFFFFF) == 0x00FF00);
  device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFFFF0000, 1.0f, 0);
  device->Present(nullptr, nullptr, nullptr, nullptr);
  device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF0000FF, 1.0f, 0);
  device->GetFrontBufferData(0, front);
  check("front buffer ignores the frame being drawn", (read(front, 10, 10) & 0xFFFFFF) == 0xFF0000);

  // Present rects: the back buffer's right half fills the window, then the whole back buffer the left half
  RECT client;
  GetClientRect(hwnd, &client);
  RECT left_half = {0, 0, 32, 64}, right_half = {32, 0, 64, 64}, window_left = {0, 0, client.right / 2, client.bottom};
  IDirect3DSurface9 *bb = nullptr;
  device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
  device->ColorFill(bb, &left_half, 0xFFFF0000);
  device->ColorFill(bb, &right_half, 0xFF00FF00);
  device->Present(&right_half, nullptr, nullptr, nullptr);
  device->GetFrontBufferData(0, front);
  check("Present source rect", (read(front, 10, 10) & 0xFFFFFF) == 0x00FF00);
  device->Present(nullptr, &window_left, nullptr, nullptr);
  device->GetFrontBufferData(0, front);
  check("Present destination rect", (read(front, 5, 10) & 0xFFFFFF) == 0xFF0000 && (read(front, 60, 10) & 0xFFFFFF) == 0);
  bb->Release();

  // an additional swap chain presents into its own window
  HWND second = window(instance, L"d3d9_misc second");
  D3DPRESENT_PARAMETERS pp2 = pp;
  pp2.hDeviceWindow = second;
  pp2.BackBufferWidth = 32;
  pp2.BackBufferHeight = 32;
  IDirect3DSwapChain9 *chain = nullptr;
  check("CreateAdditionalSwapChain", SUCCEEDED(device->CreateAdditionalSwapChain(&pp2, &chain)) && chain);
  check("additional chains aren't counted", device->GetNumberOfSwapChains() == 1);
  if (chain) {
    IDirect3DSurface9 *bb = nullptr, *bb_sys = nullptr;
    chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    D3DSURFACE_DESC desc = {};
    bb->GetDesc(&desc);
    check("additional back buffer size", desc.Width == 32 && desc.Height == 32);
    device->ColorFill(bb, nullptr, 0xFF123456);
    device->CreateOffscreenPlainSurface(32, 32, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &bb_sys, nullptr);
    device->GetRenderTargetData(bb, bb_sys);
    check("additional back buffer renders", (read(bb_sys, 3, 3) & 0xFFFFFF) == 0x123456);
    check("additional chain presents", chain->Present(nullptr, nullptr, nullptr, nullptr, 0) == D3D_OK);
    check("Present into another window", device->Present(nullptr, nullptr, second, nullptr) == D3D_OK);
    check("Present into a destroyed window fails",
          device->Present(nullptr, nullptr, (HWND)(ULONG_PTR)0xDEAD0, nullptr) == D3DERR_INVALIDCALL);
    bb_sys->Release();
    bb->Release();
    chain->Release();
  }

  // ProcessVertices transforms to screen space and lights on the CPU
  struct In {
    float x, y, z, nx, ny, nz;
    DWORD color;
  } in[2] = {{0, 0, 0.5f, 0, 0, -1, 0xFF336699}, {1, 1, 0.5f, 0, 0, -1, 0xFF336699}};
  struct Out {
    float x, y, z, rhw;
    DWORD color;
  } out[2] = {};
  IDirect3DVertexBuffer9 *src_vb = nullptr, *dst_vb = nullptr;
  device->CreateVertexBuffer(sizeof(in), 0, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &src_vb, nullptr);
  device->CreateVertexBuffer(sizeof(out), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_SYSTEMMEM, &dst_vb, nullptr);
  void *data = nullptr;
  src_vb->Lock(0, 0, &data, 0);
  memcpy(data, in, sizeof(in));
  src_vb->Unlock();
  device->SetStreamSource(0, src_vb, 0, sizeof(In));
  device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  check("ProcessVertices", device->ProcessVertices(0, 0, 2, dst_vb, nullptr, 0) == D3D_OK);
  dst_vb->Lock(0, 0, &data, D3DLOCK_READONLY);
  memcpy(out, data, sizeof(out));
  dst_vb->Unlock();
  check("ProcessVertices screen position", out[0].x == 32 && out[0].y == 32 && out[0].z == 0.5f && out[0].rhw == 1 &&
                                               out[1].x == 64 && out[1].y == 0);
  check("ProcessVertices unlit color", out[0].color == 0xFF336699);
  D3DLIGHT9 light = {};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1, 0, 0, 1};
  light.Direction = {0, 0, 1};
  device->SetLight(0, &light);
  device->LightEnable(0, TRUE);
  device->SetRenderState(D3DRS_LIGHTING, TRUE);
  device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
  D3DMATERIAL9 material = {};
  material.Diffuse = {0.5f, 1, 1, 1};
  device->SetMaterial(&material);
  device->ProcessVertices(0, 0, 1, dst_vb, nullptr, 0);
  dst_vb->Lock(0, 0, &data, D3DLOCK_READONLY);
  memcpy(out, data, sizeof(out));
  dst_vb->Unlock();
  check("ProcessVertices lighting", out[0].color == 0xFF800000);
  device->SetStreamSource(0, nullptr, 0, 0);
  src_vb->Release();
  dst_vb->Release();

  IDirect3DSurface9 *cursor = nullptr;
  device->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &cursor, nullptr);
  check("SetCursorProperties", device->SetCursorProperties(1, 1, cursor) == D3D_OK);
  check("ShowCursor reports the previous state", device->ShowCursor(TRUE) == FALSE && device->ShowCursor(TRUE) == TRUE);
  check("the cursor is set", GetCursor() != nullptr);
  device->ShowCursor(FALSE);

  for (IDirect3DSurface9 *s : {sys, rt, rt_sys, front, cursor})
    if (s)
      s->Release();

  D3DPRESENT_PARAMETERS moved = pp;
  moved.hDeviceWindow = second;
  check("Reset onto another window", SUCCEEDED(device->Reset(&moved)));
  IDirect3DSwapChain9 *implicit = nullptr;
  D3DPRESENT_PARAMETERS got_pp = {};
  device->GetSwapChain(0, &implicit);
  implicit->GetPresentParameters(&got_pp);
  implicit->Release();
  check("the swap chain follows the window", got_pp.hDeviceWindow == second);
  device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF404040, 1.0f, 0);
  check("Present after moving windows", device->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK);
  device->Release();
  d3d->Release();
  printf("%d failure(s)\n", failures);
  return failures;
}
