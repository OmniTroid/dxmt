// contract: 64-bit values cross typed resources whole. SM 6.6 reads and writes uint64_t through R32G32_UINT views, the
// low word in R and the high word in G (HLSL Shader Model 6.6, Int64 atomics).
#include "d3d12_test.hpp"

static const char hlsl[] = R"hlsl(
Texture2D<uint64_t> source : register(t0);
Buffer<uint64_t> numbers : register(t1);
RWTexture2D<uint64_t> target : register(u0);
RWBuffer<uint64_t> written : register(u1);
[numthreads(1, 1, 1)]
void cs(uint3 id : SV_DispatchThreadID) {
  target[id.xy] = source.Load(int3(id.xy, 0)) + 0x100000001ull;
  written[id.x + id.y * SIDE] = numbers[id.x + id.y * SIDE] + 0x100000001ull;
}
)hlsl";

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  if (!compiler.dxc) {
    printf("skipped: 64-bit typed resources are DXIL only\n");
    return 77;
  }
  const UINT side = 4, texels = side * side;
  const std::vector<std::string> defines = {"SIDE=" + std::to_string(side)};
  auto cs = compiler.compile(hlsl, "cs", "cs_6_6", defines);
  if (cs.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

  D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 2}};
  D3D12_ROOT_PARAMETER param{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
  param.DescriptorTable = {2, ranges};
  auto rs = root_signature(device.Get(), {1, &param});
  D3D12_COMPUTE_PIPELINE_STATE_DESC compute{rs.Get(), bytecode(cs)};
  ComPtr<ID3D12PipelineState> compute_pso;
  CHECK(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&compute_pso)));

  const D3D12_HEAP_PROPERTIES gpu{D3D12_HEAP_TYPE_DEFAULT};
  const DXGI_FORMAT words = DXGI_FORMAT_R32G32_UINT;
  D3D12_RESOURCE_DESC texture{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, side, side, 1, 1, words, {1, 0},
                              D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
  D3D12_RESOURCE_DESC linear{D3D12_RESOURCE_DIMENSION_BUFFER, 0, texels * sizeof(UINT64), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
  ComPtr<ID3D12Resource> source, target, numbers, written;
  CHECK(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&source)));
  CHECK(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&target)));
  CHECK(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_NONE, &linear, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&numbers)));
  CHECK(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_NONE, &linear, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&written)));

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  ComPtr<ID3D12DescriptorHeap> heap;
  CHECK(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)));
  auto step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto at = [&](UINT i) {
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += i * step;
    return handle;
  };
  D3D12_SHADER_RESOURCE_VIEW_DESC buffer_srv{words, D3D12_SRV_DIMENSION_BUFFER, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
  buffer_srv.Buffer = {0, texels};
  D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_uav{words, D3D12_UAV_DIMENSION_BUFFER};
  buffer_uav.Buffer = {0, texels};
  device->CreateShaderResourceView(source.Get(), nullptr, at(0));
  device->CreateShaderResourceView(numbers.Get(), &buffer_srv, at(1));
  device->CreateUnorderedAccessView(target.Get(), nullptr, nullptr, at(2));
  device->CreateUnorderedAccessView(written.Get(), nullptr, &buffer_uav, at(3));

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT64 bytes;
  device->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
  const UINT64 buffer_at = bytes, total = bytes + texels * sizeof(UINT64);
  auto upload = buffer(device.Get(), D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ);
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, total * 3, D3D12_RESOURCE_STATE_COPY_DEST);
  auto value = [](UINT i) { return UINT64(0x10000000u + i) << 32 | (0xfffffff0u + i); };
  char *mapped;
  CHECK(upload->Map(0, nullptr, (void **)&mapped));
  for (UINT i = 0; i < texels; i++) {
    *(UINT64 *)(mapped + footprint.Footprint.RowPitch * (i / side) + (i % side) * sizeof(UINT64)) = value(i);
    ((UINT64 *)(mapped + buffer_at))[i] = value(i) + 1000;
  }

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));

  auto transition = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
  };
  const D3D12_TEXTURE_COPY_LOCATION from_upload{upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {.PlacedFootprint = footprint}},
      into_source{source.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  list->CopyTextureRegion(&into_source, 0, 0, 0, &from_upload, nullptr);
  list->CopyBufferRegion(numbers.Get(), 0, upload.Get(), buffer_at, texels * sizeof(UINT64));
  transition(source.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(numbers.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  for (auto *resource : {target.Get(), written.Get()})
    transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap *heaps[] = {heap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(rs.Get());
  list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
  list->SetPipelineState(compute_pso.Get());
  list->Dispatch(side, side, 1);
  for (auto *resource : {target.Get(), written.Get()})
    transition(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto copy_out = [&](ID3D12Resource *from, UINT64 offset) {
    auto place = footprint;
    place.Offset = offset;
    const D3D12_TEXTURE_COPY_LOCATION to{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {.PlacedFootprint = place}},
        out{from, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    list->CopyTextureRegion(&to, 0, 0, 0, &out, nullptr);
  };
  const UINT64 aligned = (bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
  copy_out(target.Get(), 0);
  list->CopyBufferRegion(readback.Get(), aligned, written.Get(), 0, texels * sizeof(UINT64));
  CHECK(list->Close());
  ID3D12CommandList *lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  auto done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  CHECK(fence->SetEventOnCompletion(1, done));
  CHECK(queue->Signal(fence.Get(), 1));
  WaitForSingleObject(done, INFINITE);

  char *results;
  CHECK(readback->Map(0, nullptr, (void **)&results));
  UINT wrong = 0;
  auto compare = [&](const char *what, UINT i, UINT64 got, UINT64 want) {
    if (got != want && wrong++ < 16)
      printf("wrong: %s %u is %llx, want %llx\n", what, i, (unsigned long long)got, (unsigned long long)want);
  };
  for (UINT i = 0; i < texels; i++) {
    auto texel = [&](UINT64 base) {
      return *(UINT64 *)(results + base + footprint.Footprint.RowPitch * (i / side) + (i % side) * sizeof(UINT64));
    };
    compare("texture", i, texel(0), value(i) + 0x100000001ull);
    compare("buffer", i, ((UINT64 *)(results + aligned))[i], value(i) + 1000 + 0x100000001ull);
  }
  printf(wrong ? "failed: %u wrong results\n" : "passed: %u wrong results\n", wrong);
  return wrong != 0;
}
