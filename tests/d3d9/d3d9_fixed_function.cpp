#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

static int failures = 0;
static IDirect3DDevice9 *device;
static IDirect3DSurface9 *rt, *readback;

static bool
close(DWORD a, DWORD b, int tolerance) {
  for (int shift = 0; shift < 32; shift += 8) {
    int x = (a >> shift) & 0xFF, y = (b >> shift) & 0xFF;
    if (x - y > tolerance || y - x > tolerance)
      return false;
  }
  return true;
}

static DWORD
pixel(int x, int y) {
  if (FAILED(device->GetRenderTargetData(rt, readback)))
    return 0xDEADBEEF;
  D3DLOCKED_RECT lr;
  if (FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
    return 0xDEADBEEF;
  DWORD value = ((DWORD *)((char *)lr.pBits + y * lr.Pitch))[x];
  readback->UnlockRect();
  return value;
}

static void
expect(const char *name, int x, int y, DWORD want, int tolerance = 3) {
  DWORD got = pixel(x, y);
  bool ok = close(got, want, tolerance);
  if (ok)
    printf("PASS: %s\n", name);
  else
    printf("FAIL: %s (got %08lx want %08lx)\n", name, got, want);
  failures += !ok;
}

static void
clear() {
  device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
}

struct ScreenVertex {
  float x, y, z, rhw;
  DWORD color;
  float u, v;
};

// covers the pixel centers x0 .. x1 - 1, y0 .. y1 - 1
static void
screen_quad(float x0, float y0, float x1, float y1, DWORD color) {
  ScreenVertex v[4] = {
      {x0, y0, 0.5f, 1, color, 0, 0},
      {x1, y0, 0.5f, 1, color, 1, 0},
      {x0, y1, 0.5f, 1, color, 0, 1},
      {x1, y1, 0.5f, 1, color, 1, 1},
  };
  device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
  device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(ScreenVertex));
}

struct LitVertex {
  float x, y, z;
  float nx, ny, nz;
};

static void
lit_quad(float nx, float ny, float nz) {
  LitVertex v[4] = {
      {-1, -1, 0.5f, nx, ny, nz},
      {-1, 1, 0.5f, nx, ny, nz},
      {1, -1, 0.5f, nx, ny, nz},
      {1, 1, 0.5f, nx, ny, nz},
  };
  device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL);
  device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(LitVertex));
}

static void
stage(DWORD index, DWORD color_op, DWORD color_arg1, DWORD color_arg2, DWORD alpha_op, DWORD alpha_arg1) {
  device->SetTextureStageState(index, D3DTSS_COLOROP, color_op);
  device->SetTextureStageState(index, D3DTSS_COLORARG1, color_arg1);
  device->SetTextureStageState(index, D3DTSS_COLORARG2, color_arg2);
  device->SetTextureStageState(index, D3DTSS_ALPHAOP, alpha_op);
  device->SetTextureStageState(index, D3DTSS_ALPHAARG1, alpha_arg1);
}

static void
default_stages() {
  stage(0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEXTURE);
  stage(1, D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_DISABLE, D3DTA_TEXTURE);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = L"d3d9_fixed_function";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr, instance, nullptr);

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
  device->CreateRenderTarget(64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr);
  device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr);
  device->SetRenderTarget(0, rt);
  device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  device->SetRenderState(D3DRS_ZENABLE, FALSE);
  device->BeginScene();

  // default stages modulate an unbound texture, which passes the vertex color through
  clear();
  screen_quad(0, 0, 32, 64, 0xFF204080);
  expect("pretransformed vertex color", 16, 32, 0xFF204080);
  expect("pretransformed position", 48, 32, 0xFF000000);

  D3DVIEWPORT9 viewport = {32, 0, 32, 64, 0, 1};
  device->SetViewport(&viewport);
  clear();
  screen_quad(32, 0, 48, 64, 0xFFFFFFFF);
  expect("pretransformed position ignores the viewport origin", 40, 32, 0xFFFFFFFF);
  expect("pretransformed quad stays inside its rect", 56, 32, 0xFF000000);
  viewport = {0, 0, 64, 64, 0, 1};
  device->SetViewport(&viewport);

  IDirect3DTexture9 *texture = nullptr;
  device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
  D3DLOCKED_RECT lr;
  texture->LockRect(0, &lr, nullptr, 0);
  for (int y = 0; y < 2; y++)
    for (int x = 0; x < 2; x++)
      ((DWORD *)((char *)lr.pBits + y * lr.Pitch))[x] = x ? 0x80FF0000 : 0xFF00FF00;
  texture->UnlockRect(0);
  device->SetTexture(0, texture);
  clear();
  screen_quad(0, 0, 64, 64, 0xFF808080);
  expect("texture modulates diffuse", 16, 32, 0xFF008000);
  expect("texture alpha selects texture", 48, 32, 0x80800000);
  device->SetTexture(0, nullptr);

  device->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF123456);
  stage(0, D3DTOP_SELECTARG1, D3DTA_TFACTOR, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TFACTOR);
  clear();
  screen_quad(0, 0, 64, 64, 0xFFFFFFFF);
  expect("select texture factor", 32, 32, 0xFF123456);

  device->SetRenderState(D3DRS_TEXTUREFACTOR, 0x00102030);
  stage(0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
  stage(1, D3DTOP_ADD, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_SELECTARG1, D3DTA_CURRENT);
  clear();
  screen_quad(0, 0, 64, 64, 0xFF402000);
  expect("second stage adds texture factor", 32, 32, 0xFF504030);

  stage(0, D3DTOP_SELECTARG1, D3DTA_DIFFUSE | D3DTA_COMPLEMENT, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_DIFFUSE);
  stage(1, D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_DISABLE, D3DTA_TEXTURE);
  clear();
  screen_quad(0, 0, 64, 64, 0xFF204060);
  expect("complemented argument", 32, 32, 0xFFDFBF9F);
  default_stages();

  const char *ps_source = "float4 main(float4 c : COLOR0) : COLOR0 { return c.bgra; }";
  ID3DBlob *code = nullptr;
  IDirect3DPixelShader9 *ps = nullptr;
  if (SUCCEEDED(D3DCompile(ps_source, strlen(ps_source), nullptr, nullptr, nullptr, "main", "ps_2_0", 0, 0, &code, nullptr)))
    device->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps);
  device->SetPixelShader(ps);
  clear();
  screen_quad(0, 0, 64, 64, 0xFF204080);
  expect("fixed-function vertex stage feeds a pixel shader", 32, 32, 0xFF804020);
  device->SetPixelShader(nullptr);

  struct ColorVertex {
    float x, y, z;
    DWORD color;
  } small[4] = {
      {-0.25f, -0.25f, 0.5f, 0xFF00FF00},
      {-0.25f, 0.25f, 0.5f, 0xFF00FF00},
      {0.25f, -0.25f, 0.5f, 0xFF00FF00},
      {0.25f, 0.25f, 0.5f, 0xFF00FF00},
  };
  D3DMATRIX world = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5f, 0, 0, 1}}};
  D3DMATRIX identity = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
  D3DMATRIX scale = {{{0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  device->SetTransform(D3DTS_WORLD, &world);
  device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  clear();
  device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, small, sizeof(ColorVertex));
  // world moves the quad to x 0.25 .. 0.75, i.e. pixels 40 .. 55
  expect("world transform moves the quad", 48, 32, 0xFF00FF00);
  expect("world transform leaves the center", 32, 32, 0xFF000000);
  device->SetTransform(D3DTS_WORLD, &identity);
  device->SetTransform(D3DTS_VIEW, &scale);
  device->MultiplyTransform(D3DTS_VIEW, &world);
  // view = translate * scale: x 0.125 .. 0.375, pixels 36 .. 43
  clear();
  device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, small, sizeof(ColorVertex));
  expect("view transform composes with world", 39, 32, 0xFF00FF00);
  expect("view transform scales the quad", 46, 32, 0xFF000000);
  D3DMATRIX got;
  device->GetTransform(D3DTS_VIEW, &got);
  bool round_trip = got.m[3][0] == 0.25f && got.m[0][0] == 0.5f;
  printf("%s: MultiplyTransform result\n", round_trip ? "PASS" : "FAIL");
  failures += !round_trip;
  device->SetTransform(D3DTS_VIEW, &identity);

  device->SetRenderState(D3DRS_LIGHTING, TRUE);
  device->SetRenderState(D3DRS_AMBIENT, 0xFF404040);
  D3DMATERIAL9 material = {};
  material.Diffuse = {1, 1, 1, 1};
  material.Ambient = {1, 0.5f, 1, 1};
  device->SetMaterial(&material);
  clear();
  lit_quad(0, 0, -1);
  expect("ambient lighting", 32, 32, 0xFF402040);

  D3DLIGHT9 light = {};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1, 0, 0, 1};
  light.Direction = {0, 0, 1};
  device->SetLight(0, &light);
  device->LightEnable(0, TRUE);
  device->SetRenderState(D3DRS_AMBIENT, 0);
  clear();
  lit_quad(0, 0, -1);
  expect("directional light facing the surface", 32, 32, 0xFFFF0000);
  clear();
  lit_quad(0, 0.8f, -0.6f);
  expect("directional light at an angle", 32, 32, 0xFF990000);
  clear();
  lit_quad(1, 0, 0);
  expect("directional light parallel to the surface", 32, 32, 0xFF000000);
  device->SetRenderState(D3DRS_AMBIENT, 0xFF404040);
  clear();
  lit_quad(0, 0, 1);
  expect("light from behind adds nothing to ambient", 32, 32, 0xFF402040);
  BOOL enabled = FALSE;
  device->GetLightEnable(0, &enabled);
  printf("%s: GetLightEnable\n", enabled ? "PASS" : "FAIL");
  failures += !enabled;

  device->EndScene();
  if (ps)
    ps->Release();
  texture->Release();
  readback->Release();
  rt->Release();
  device->Release();
  d3d->Release();
  printf("%d failure(s)\n", failures);
  return failures;
}
