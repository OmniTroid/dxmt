// contract: a texture on a D3D12_HEAP_FLAG_SHARED heap has an NT handle, and what opening that handle gives is the same
// memory (ID3D12Device::CreateSharedHandle, OpenSharedHandle): a clear through one device reads back through another.
#include "d3d12_test.hpp"

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  const UINT side = 4;
  const D3D12_HEAP_PROPERTIES gpu{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                           D3D12_TEXTURE_LAYOUT_UNKNOWN,
                           D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS};
  ComPtr<ID3D12Resource> texture, opened;
  CHECK(device->CreateCommittedResource(&gpu, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture)));
  HANDLE handle = nullptr;
  CHECK(device->CreateSharedHandle(texture.Get(), nullptr, GENERIC_ALL, nullptr, &handle));
  CHECK(device->OpenSharedHandle(handle, IID_PPV_ARGS(&opened)));
  CloseHandle(handle);
  auto got = opened->GetDesc();
  UINT wrong = 0;
  if (got.Width != desc.Width || got.Height != desc.Height || got.Format != desc.Format) {
    printf("wrong: the opened texture's description differs\n");
    wrong++;
  }

  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
  CHECK(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)));
  auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(texture.Get(), nullptr, rtv);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT64 bytes;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST);

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
  const FLOAT colour[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  transition(texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->ClearRenderTargetView(rtv, colour, 0, nullptr);
  transition(texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
  transition(opened.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
  const D3D12_TEXTURE_COPY_LOCATION to{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {.PlacedFootprint = footprint}},
      from{opened.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  CHECK(list->Close());
  ID3D12CommandList *lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  auto done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  CHECK(fence->SetEventOnCompletion(1, done));
  CHECK(queue->Signal(fence.Get(), 1));
  WaitForSingleObject(done, INFINITE);

  char *mapped;
  CHECK(readback->Map(0, nullptr, (void **)&mapped));
  for (UINT y = 0; y < side; y++)
    for (UINT x = 0; x < side; x++) {
      auto texel = *(UINT *)(mapped + footprint.Footprint.RowPitch * y + x * 4);
      if (texel != 0xff00ff00 && wrong++ < 8)
        printf("wrong: texel %u,%u is %08x\n", x, y, texel);
    }
  printf(wrong ? "failed: %u wrong results\n" : "passed: %u wrong results\n", wrong);
  return wrong != 0;
}
