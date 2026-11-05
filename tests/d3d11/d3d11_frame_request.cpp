// contract: with DXMT_FRAME_REQUEST=<dir>, creating <dir>/request saves one presented frame as a BMP in <dir>, with
// the pixels the frame holds, and removes the request. nothing is saved without a request.
#include "d3d11_test.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::string>
frames_in(const std::string &directory) {
  std::vector<std::string> names;
  WIN32_FIND_DATAA found;
  auto find = FindFirstFileA((directory + "/frame-*.bmp").c_str(), &found);
  if (find == INVALID_HANDLE_VALUE)
    return names;
  do
    names.push_back(found.cFileName);
  while (FindNextFileA(find, &found));
  FindClose(find);
  return names;
}

int
main() {
  char temp[MAX_PATH], unique[MAX_PATH];
  GetTempPathA(MAX_PATH, temp);
  GetTempFileNameA(temp, "dxf", 0, unique);
  DeleteFileA(unique);
  CreateDirectoryA(unique, nullptr);
  const std::string directory = unique, request = directory + "/request";
  SetEnvironmentVariableA("DXMT_FRAME_REQUEST", unique);

  const UINT width = 96, height = 64;
  WNDCLASSA wc{0, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "frame_request"};
  RegisterClassA(&wc);
  auto window = CreateWindowA(
      "frame_request", "frame_request", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr,
      wc.hInstance, nullptr
  );
  DXGI_SWAP_CHAIN_DESC desc{{width, height, {60, 1}, DXGI_FORMAT_R8G8B8A8_UNORM}, {1, 0},
                            DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, window, TRUE, DXGI_SWAP_EFFECT_FLIP_DISCARD};
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGISwapChain> swapchain;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  CHECK(D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &desc, &swapchain, &device, nullptr,
      &context
  ));
  auto present = [&] {
    ComPtr<ID3D11Texture2D> back;
    ComPtr<ID3D11RenderTargetView> view;
    swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    device->CreateRenderTargetView(back.Get(), nullptr, &view);
    const float color[4] = {1, 0, 0.2f, 1};
    context->ClearRenderTargetView(view.Get(), color);
    swapchain->Present(0, 0);
  };

  for (int i = 0; i < 30; i++) {
    present();
    Sleep(10);
  }
  expect(frames_in(directory).empty(), "a frame was saved without a request");

  CloseHandle(CreateFileA(request.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr));
  std::vector<std::string> saved;
  for (int i = 0; i < 300 && saved.empty(); i++) {
    present();
    Sleep(10);
    saved = frames_in(directory);
  }
  if (!expect(saved.size() == 1, "%zu frames saved for one request", saved.size()))
    return verdict();
  expect(GetFileAttributesA(request.c_str()) == INVALID_FILE_ATTRIBUTES, "the request was not removed");

  const auto path = directory + "/" + saved[0];
  auto file = fopen(path.c_str(), "rb");
  if (!expect(file != nullptr, "the frame can't be opened"))
    return verdict();
  uint8_t header[54];
  const size_t bytes = width * height * 4;
  std::vector<uint8_t> pixels(bytes);
  expect(fread(header, sizeof(header), 1, file) == 1 && fread(pixels.data(), bytes, 1, file) == 1, "the frame is short");
  fclose(file);
  int32_t got_width, got_height;
  memcpy(&got_width, header + 18, 4);
  memcpy(&got_height, header + 22, 4);
  expect(header[0] == 'B' && header[1] == 'M' && got_width == int32_t(width) && got_height == -int32_t(height),
         "header says %dx%d", got_width, got_height);
  for (size_t i = 0; i < bytes; i += 4)
    if (!expect(pixels[i] == 51 && pixels[i + 1] == 0 && pixels[i + 2] == 255 && pixels[i + 3] == 255,
                "pixel %zu is BGRA %u %u %u %u", i / 4, pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]))
      break;

  for (int i = 0; i < 40; i++) {
    present();
    Sleep(10);
  }
  expect(frames_in(directory).size() == 1, "one request saved more than one frame");
  DeleteFileA(path.c_str());
  RemoveDirectoryA(unique);
  return verdict();
}
