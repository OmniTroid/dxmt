// Checks that textures created with initial data hold all of it, read back through a
// staging copy. D3D11 ignores SysMemPitch for 1D textures, so games pass anything there.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

static ID3D11Device *device;
static ID3D11DeviceContext *context;

static uint32_t
pixel(UINT x, UINT y) {
  return 0xff000000u | (y << 16) | ((x * 7u) & 0xffffu);
}

static bool
check(const char *name, const uint8_t *mapped, UINT row_pitch, UINT width, UINT height) {
  for (UINT y = 0; y < height; y++) {
    const uint32_t *row = (const uint32_t *)(mapped + (size_t)y * row_pitch);
    for (UINT x = 0; x < width; x++) {
      if (row[x] != pixel(x, y)) {
        printf("FAIL %s: pixel (%u,%u) is %08x, want %08x\n", name, x, y, row[x], pixel(x, y));
        return false;
      }
    }
  }
  printf("PASS %s\n", name);
  return true;
}

static bool
test_1d(const char *name, D3D11_USAGE usage, UINT pitch) {
  const UINT width = 256;
  std::vector<uint32_t> data(width);
  for (UINT x = 0; x < width; x++)
    data[x] = pixel(x, 0);

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = width;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.Usage = usage;
  if (usage == D3D11_USAGE_STAGING) {
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  } else {
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
  }
  D3D11_SUBRESOURCE_DATA init = {data.data(), pitch, 0};
  ID3D11Texture1D *tex = nullptr;
  if (FAILED(device->CreateTexture1D(&desc, &init, &tex))) {
    printf("FAIL %s: CreateTexture1D\n", name);
    return false;
  }

  ID3D11Texture1D *readback = tex;
  if (usage != D3D11_USAGE_STAGING) {
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture1D(&desc, nullptr, &readback))) {
      printf("FAIL %s: CreateTexture1D (staging)\n", name);
      tex->Release();
      return false;
    }
    context->CopyResource(readback, tex);
  }

  D3D11_MAPPED_SUBRESOURCE mapped;
  bool ok = false;
  if (FAILED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped))) {
    printf("FAIL %s: Map\n", name);
  } else {
    ok = check(name, (const uint8_t *)mapped.pData, mapped.RowPitch, width, 1);
    context->Unmap(readback, 0);
  }
  if (readback != tex)
    readback->Release();
  tex->Release();
  return ok;
}

static bool
test_2d_dynamic_padded(const char *name) {
  const UINT width = 64, height = 4, pitch = width * 4 + 64;
  std::vector<uint8_t> data(pitch * height, 0xcd);
  for (UINT y = 0; y < height; y++)
    for (UINT x = 0; x < width; x++)
      ((uint32_t *)(data.data() + y * pitch))[x] = pixel(x, y);

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  D3D11_SUBRESOURCE_DATA init = {data.data(), pitch, 0};
  ID3D11Texture2D *tex = nullptr, *readback = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &init, &tex))) {
    printf("FAIL %s: CreateTexture2D\n", name);
    return false;
  }
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback))) {
    printf("FAIL %s: CreateTexture2D (staging)\n", name);
    tex->Release();
    return false;
  }
  context->CopyResource(readback, tex);
  D3D11_MAPPED_SUBRESOURCE mapped;
  bool ok = false;
  if (FAILED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped))) {
    printf("FAIL %s: Map\n", name);
  } else {
    ok = check(name, (const uint8_t *)mapped.pData, mapped.RowPitch, width, height);
    context->Unmap(readback, 0);
  }
  readback->Release();
  tex->Release();
  return ok;
}

int
main() {
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device,
                               nullptr, &context))) {
    printf("FAIL D3D11CreateDevice\n");
    return 1;
  }
  bool ok = true;
  ok &= test_1d("1d dynamic, pitch = width", D3D11_USAGE_DYNAMIC, 256);
  ok &= test_1d("1d dynamic, pitch = 0", D3D11_USAGE_DYNAMIC, 0);
  ok &= test_1d("1d staging, pitch = 0", D3D11_USAGE_STAGING, 0);
  ok &= test_1d("1d default, pitch = width", D3D11_USAGE_DEFAULT, 256);
  ok &= test_2d_dynamic_padded("2d dynamic, padded pitch");
  context->Release();
  device->Release();
  return ok ? 0 : 1;
}
