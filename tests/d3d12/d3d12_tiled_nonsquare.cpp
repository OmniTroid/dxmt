// contract: CheckAccessFullyMapped "returns true if all data being accessed was mapped in the resource" (D3D11.3
// 5.9.4.6). DXMT can't meet it for a writable reserved texture whose Metal tiles are not square, on an M3 Max with
// macOS 27.2: Metal reports the tile with its axes swapped and the top bit of y dropped, which a shader can't undo
// (docs/METAL_NOTES.md). this asserts that behavior, so a macOS that fixes it fails here. the data is right.
#include "d3d12_test.hpp"
#include "../../src/winemetal/winemetal.h"

static const char hlsl[] = R"hlsl(
RWTexture2D<uint4> texels : register(u0);
[numthreads(8, 8, 1)] void write(uint3 t : SV_DispatchThreadID) {
  texels[t.xy] = uint4(1 + t.x + t.y * SIZE, 0, 0, 0);
}

Texture2D<uint4> source : register(t0);
RWStructuredBuffer<uint2> o : register(u1);
[numthreads(8, 8, 1)] void read(uint3 t : SV_DispatchThreadID) {
  uint status;
  uint value = source.Load(int3(t.xy, 0), int2(0, 0), status).x;
  o[t.x + t.y * SIZE] = uint2(value, CheckAccessFullyMapped(status));
}
)hlsl";

static const UINT size = 1024;

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  auto bridge = GetModuleHandleA("winemetal.dll");
  if (!bridge)
    bridge = LoadLibraryA("winemetal.dll");
  auto os_version = reinterpret_cast<decltype(&WMTGetOSVersion)>(GetProcAddress(bridge, "WMTGetOSVersion"));
  auto copy_devices = reinterpret_cast<decltype(&WMTCopyAllDevices)>(GetProcAddress(bridge, "WMTCopyAllDevices"));
  auto supports =
      reinterpret_cast<decltype(&MTLDevice_supportsFamily)>(GetProcAddress(bridge, "MTLDevice_supportsFamily"));
  auto release = reinterpret_cast<decltype(&NSObject_release)>(GetProcAddress(bridge, "NSObject_release"));
  auto at = reinterpret_cast<decltype(&NSArray_object)>(GetProcAddress(bridge, "NSArray_object"));
  if (!os_version || !copy_devices || !supports || !at || !release) {
    printf("failed: the Metal bridge does not export what this test needs\n");
    return 1;
  }
  uint64_t macos_major = 0;
  os_version(&macos_major, nullptr, nullptr);
  auto devices = copy_devices();
  const bool apple10 = supports(at(devices, 0), WMTGPUFamilyApple10);
  release(devices);
  if (macos_major != 27 || apple10) {
    printf("passed: not measured on this macOS and GPU\n");
    return 0;
  }

  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_RESOURCE_DESC desc{
      D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      0,
      size,
      size,
      1,
      1,
      DXGI_FORMAT_R16G16B16A16_UINT,
      {1, 0},
      D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
  };
  ComPtr<ID3D12Resource> texture;
  CHECK(device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture)));
  D3D12_TILE_SHAPE shape{};
  UINT total = 0;
  device->GetResourceTiling(texture.Get(), &total, nullptr, &shape, nullptr, 0, nullptr);
  if (shape.WidthInTexels != 128 || shape.HeightInTexels != 64) {
    printf("failed: a 64-bit tile of %ux%u texels\n", shape.WidthInTexels, shape.HeightInTexels);
    return 1;
  }

  std::vector<std::string> defines = {"SIZE=" + std::to_string(size)};
  auto write = compiler.compile(hlsl, "write", "cs", defines), read = compiler.compile(hlsl, "read", "cs", defines);
  if (write.empty() || read.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0}};
  D3D12_ROOT_PARAMETER params[3] = {
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},
      {D3D12_ROOT_PARAMETER_TYPE_UAV}
  };
  params[0].DescriptorTable = {1, &ranges[0]};
  params[1].DescriptorTable = {1, &ranges[1]};
  params[2].Descriptor = {1, 0};
  auto rs = root_signature(device.Get(), {3, params});
  ComPtr<ID3D12PipelineState> write_pso, read_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC cs{rs.Get(), bytecode(write)};
  CHECK(device->CreateComputePipelineState(&cs, IID_PPV_ARGS(&write_pso)));
  cs.CS = bytecode(read);
  CHECK(device->CreateComputePipelineState(&cs, IID_PPV_ARGS(&read_pso)));

  ComPtr<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
  };
  CHECK(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptors)));
  auto step_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
  device->CreateUnorderedAccessView(texture.Get(), nullptr, nullptr, cpu);
  device->CreateShaderResourceView(texture.Get(), nullptr, {cpu.ptr + step_size});

  ComPtr<ID3D12Heap> heap;
  D3D12_HEAP_DESC tiles{
      D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES,
      {D3D12_HEAP_TYPE_DEFAULT},
      0,
      D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES
  };
  CHECK(device->CreateHeap(&tiles, IID_PPV_ARGS(&heap)));
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  // tile (2, 0) is Metal's 64x32 tiles (4, 0), (5, 0), (4, 1) and (5, 1), which Metal reports at (0, 4) to (1, 5)
  const D3D12_TILED_RESOURCE_COORDINATE mapped{2, 0, 0, 0};
  D3D12_TILE_REGION_SIZE one{1, FALSE};
  D3D12_TILE_RANGE_FLAGS range = D3D12_TILE_RANGE_FLAG_NONE;
  UINT start = 0, length = 1;
  queue->UpdateTileMappings(
      texture.Get(), 1, &mapped, &one, heap.Get(), 1, &range, &start, &length, D3D12_TILE_MAPPING_FLAG_NONE
  );

  const UINT64 bytes = UINT64(size) * size * 8;
  auto out = buffer(
      device.Get(), D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
  );
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), write_pso.Get(), IID_PPV_ARGS(&list)
  ));
  ID3D12DescriptorHeap *heaps[] = {descriptors.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(rs.Get());
  list->SetComputeRootDescriptorTable(0, gpu);
  list->Dispatch(size / 8, size / 8, 1);
  CHECK(submit(device.Get(), queue.Get(), list.Get()));

  step("read every texel");
  CHECK(allocator->Reset());
  CHECK(list->Reset(allocator.Get(), read_pso.Get()));
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(rs.Get());
  transition(
      list.Get(), texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
  );
  list->SetComputeRootDescriptorTable(1, {gpu.ptr + step_size});
  list->SetComputeRootUnorderedAccessView(2, out->GetGPUVirtualAddress());
  list->Dispatch(size / 8, size / 8, 1);
  transition(list.Get(), out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.Get(), 0, out.Get(), 0, bytes);
  CHECK(submit(device.Get(), queue.Get(), list.Get()));

  UINT *o;
  CHECK(readback->Map(0, nullptr, reinterpret_cast<void **>(&o)));
  unsigned failures = 0;
  for (UINT y = 0; y < size; y++)
    for (UINT x = 0; x < size; x++) {
      const bool holds = x >= 256 && x < 384 && y < 64;
      const bool reported = x < 128 && y >= 128 && y < 192;
      const UINT value = o[2 * (x + y * size)], status = o[2 * (x + y * size) + 1];
      if (value != (holds ? 1 + x + y * size : 0) || status != UINT(reported)) {
        if (failures++ < 16)
          printf("(%u, %u): %u, mapped %u; want %u, mapped %u\n", x, y, value, status, holds ? 1 + x + y * size : 0,
                 UINT(reported));
      }
    }
  readback->Unmap(0, nullptr);
  if (failures) {
    printf("failed: %u wrong texels\n", failures);
    return 1;
  }
  printf("passed: 0 wrong texels\n");
  return 0;
}
