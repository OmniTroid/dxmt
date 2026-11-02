// expensive D3D12 renders with no window or swap chain: everything lands in offscreen targets, so neither the window
// server nor a display paces them. tests/perf/bench.py runs them and compares builds; not a functional test.
//
//   d3d12_render_bench.exe <dxbc|dxil> <scene> <frames> <warmup>
//   d3d12_render_bench.exe <dxbc|dxil> list
//
// `frame` has the shape measured of a real UE5 title's frame (Black Myth: Wukong's benchmark on DXMT, median): 1100
// draws, 244 passes, 3.8 million indices and 178 ExecuteIndirect calls. the other scenes each isolate one of its costs.
// frames are serialized, so `wait` is the GPU's time and `record` and `submit` are the CPU's. each timed frame prints
//   frame <index> <record ms> <submit ms> <wait ms> <gpu ms by timestamps>
// and the end prints the hash of the final image, a change of which means the output changed, not only its speed:
//   result <scene> <hash> <one-time setup or pipeline ms>
#include "../d3d12/d3d12_test.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <random>

// a benchmark has no result to give once a call fails
#undef CHECK
#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    HRESULT hr_ = (x);                                                                                                 \
    if (FAILED(hr_)) {                                                                                                 \
      printf("failed: %s = 0x%08lx (line %d)\n", #x, hr_, __LINE__);                                                    \
      ExitProcess(1);                                                                                                  \
    }                                                                                                                  \
  } while (0)

static const char hlsl[] = R"hlsl(
cbuffer Constants : register(b0) { float4 place; float4 tint; };
Texture2D<float4> textures[16] : register(t0);
RWTexture2D<float4> image : register(u0);
SamplerState linear_wrap : register(s0);

struct V { float4 pos : SV_Position; float2 uv : UV; float3 normal : NORMAL; };
V vs_grid(uint id : SV_VertexID) {
  uint2 cell = uint2(id % (GRID + 1), id / (GRID + 1));
  float2 uv = float2(cell) / GRID;
  float wave = sin(uv.x * 25 + place.z) * cos(uv.y * 25 + place.w);
  V v;
  v.pos = float4(place.xy + (uv - 0.5) * 0.3, 0.5 + 0.2 * wave, 1);
  v.uv = uv * 4;
  v.normal = normalize(float3(cos(uv.x * 25), 1, sin(uv.y * 25)));
  return v;
}
struct G { float4 albedo : SV_Target0; float4 normal : SV_Target1; float4 material : SV_Target2; float4 extra : SV_Target3; };
G ps_gbuffer(V v) {
  G g;
  float4 a = textures[0].Sample(linear_wrap, v.uv), b = textures[1].Sample(linear_wrap, v.uv * 2.3);
  float4 c = textures[2].Sample(linear_wrap, v.uv * 5.1), d = textures[3].Sample(linear_wrap, v.uv * 11.7);
  g.albedo = a * b * tint + SALT * 1e-6;
  g.normal = float4(normalize(v.normal + (c.xyz - 0.5)), d.x);
  g.material = float4(c.w, d.yz, a.w);
  g.extra = float4(v.uv, b.xy);
  return g;
}
float4 vs_full(uint id : SV_VertexID) : SV_Position {
  return float4(id == 1 ? 3 : -1, id == 2 ? -3 : 1, 0, 1);
}
float4 ps_light(float4 pos : SV_Position) : SV_Target {
  int3 at = int3(pos.xy, 0);
  float4 albedo = textures[0].Load(at), normal = textures[1].Load(at), material = textures[2].Load(at);
  float3 sum = 0;
  for (int i = 0; i < LIGHTS; i++) {
    float a = i * 0.39;
    float3 l = normalize(float3(cos(a), 0.6, sin(a)));
    float3 h = normalize(l + float3(0, 1, 0));
    float diffuse = saturate(dot(normal.xyz, l));
    float specular = pow(saturate(dot(normal.xyz, h)), 8 + 64 * material.x);
    sum += albedo.xyz * diffuse + specular * material.y;
  }
  return float4(sum / LIGHTS, 1);
}
float4 ps_flat(V v) : SV_Target { return tint * v.uv.x; }
[numthreads(8, 8, 1)] void cs_filter(uint3 id : SV_DispatchThreadID) {
  int3 at = int3(id.xy, 0);
  float4 sum = 0;
  for (int y = -2; y <= 2; y++)
    for (int x = -2; x <= 2; x++)
      sum += textures[0].Load(at + int3(x, y, 0));
  image[id.xy] = sum / 25;
}
)hlsl";

static const UINT W = 1920, H = 1080, GRID = 34, kAllocators = 64, kLists = 8, kTimestamps = 32;

struct Bench {
  Compiler compiler;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  HANDLE event = nullptr;
  UINT64 fence_value = 0;
  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12CommandSignature> draw_indirect;
  ComPtr<ID3D12DescriptorHeap> views, rtvs, dsvs;
  UINT view_step = 0, rtv_step = 0;
  ComPtr<ID3D12Resource> gbuffer[4], depth, hdr, filtered, small_target, indices, indirect_args, readback, stamps;
  ComPtr<ID3D12QueryHeap> query_heap;
  std::vector<ComPtr<ID3D12Resource>> textures;
  ComPtr<ID3D12PipelineState> pso_gbuffer[4], pso_light, pso_flat, pso_filter;
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
  std::mt19937 random{12345};
  UINT stamp = 0;
  double frequency = 0;

  D3D12_CPU_DESCRIPTOR_HANDLE cpu_view(UINT i) { auto h = views->GetCPUDescriptorHandleForHeapStart(); h.ptr += i * view_step; return h; }
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_view(UINT i) { auto h = views->GetGPUDescriptorHandleForHeapStart(); h.ptr += i * view_step; return h; }
  D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT i) { auto h = rtvs->GetCPUDescriptorHandleForHeapStart(); h.ptr += i * rtv_step; return h; }

  void wait() {
    CHECK(queue->Signal(fence.Get(), ++fence_value));
    CHECK(fence->SetEventOnCompletion(fence_value, event));
    WaitForSingleObject(event, INFINITE);
  }
};

static double now_ms() {
  static double period = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1000.0 / f.QuadPart; }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart * period;
}

static ComPtr<ID3D12Resource>
texture(Bench &b, UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear = nullptr) {
  D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, format, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags};
  ComPtr<ID3D12Resource> res;
  CHECK(b.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear, IID_PPV_ARGS(&res)));
  return res;
}

static std::vector<std::string> defines(UINT salt = 0) {
  return {"GRID=" + std::to_string(GRID), "LIGHTS=32", "SALT=" + std::to_string(salt)};
}

static ComPtr<ID3D12PipelineState>
graphics(Bench &b, const std::string &vs, const std::string &ps, UINT targets, DXGI_FORMAT format, bool depth, D3D12_CULL_MODE cull) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
  d.pRootSignature = b.rs.Get();
  d.VS = bytecode(vs), d.PS = bytecode(ps);
  for (UINT i = 0; i < 8; i++)
    d.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  d.SampleMask = ~0u;
  d.RasterizerState = {D3D12_FILL_MODE_SOLID, cull};
  d.DepthStencilState.DepthEnable = depth;
  d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  d.DSVFormat = depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
  d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  d.NumRenderTargets = targets;
  for (UINT i = 0; i < targets; i++)
    d.RTVFormats[i] = format;
  d.SampleDesc = {1, 0};
  ComPtr<ID3D12PipelineState> pso;
  CHECK(b.device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)));
  return pso;
}

static void setup(Bench &b) {
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&b.device)));
  auto device = b.device.Get();
  D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&b.queue)));
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&b.fence)));
  b.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  UINT64 ticks;
  CHECK(b.queue->GetTimestampFrequency(&ticks));
  b.frequency = double(ticks);

  D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 16, 0, 0, 0}, uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  D3D12_ROOT_PARAMETER params[3] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS},
                                    {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},
                                    {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}};
  params[0].Constants = {0, 0, 8};
  params[1].DescriptorTable = {1, &srv};
  params[2].DescriptorTable = {1, &uav};
  D3D12_STATIC_SAMPLER_DESC sampler{D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP};
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  b.rs = root_signature(device, {3, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE});

  D3D12_INDIRECT_ARGUMENT_DESC arg{D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED};
  D3D12_COMMAND_SIGNATURE_DESC sig{sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), 1, &arg};
  CHECK(device->CreateCommandSignature(&sig, nullptr, IID_PPV_ARGS(&b.draw_indirect)));

  D3D12_DESCRIPTOR_HEAP_DESC vd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8}, dd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
  CHECK(device->CreateDescriptorHeap(&vd, IID_PPV_ARGS(&b.views)));
  CHECK(device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&b.rtvs)));
  CHECK(device->CreateDescriptorHeap(&dd, IID_PPV_ARGS(&b.dsvs)));
  b.view_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  b.rtv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  const auto rt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  const DXGI_FORMAT color = DXGI_FORMAT_R16G16B16A16_FLOAT;
  for (UINT i = 0; i < 4; i++) {
    b.gbuffer[i] = texture(b, W, H, color, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    device->CreateRenderTargetView(b.gbuffer[i].Get(), nullptr, b.rtv(i));
  }
  b.hdr = texture(b, W, H, color, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
  device->CreateRenderTargetView(b.hdr.Get(), nullptr, b.rtv(4));
  b.small_target = texture(b, 512, 512, color, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
  device->CreateRenderTargetView(b.small_target.Get(), nullptr, b.rtv(5));
  b.filtered = texture(b, W, H, color, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12_CLEAR_VALUE clear{DXGI_FORMAT_D32_FLOAT};
  clear.DepthStencil.Depth = 1;
  b.depth = texture(b, W, H, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear);
  device->CreateDepthStencilView(b.depth.Get(), nullptr, b.dsvs->GetCPUDescriptorHandleForHeapStart());

  // 0..15 material textures; 16..31 the G-buffer first; 32..47 the lit image first; 48 the filter's image
  auto upload = buffer(device, D3D12_HEAP_TYPE_UPLOAD, 256 * 256 * 4 * 4, D3D12_RESOURCE_STATE_GENERIC_READ);
  UINT *texels;
  CHECK(upload->Map(0, nullptr, (void **)&texels));
  for (UINT i = 0; i < 256 * 256 * 4; i++)
    texels[i] = b.random();
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
  for (UINT i = 0; i < 4; i++) {
    auto t = texture(b, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{t.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, from{upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    from.PlacedFootprint = {UINT64(i) * 256 * 256 * 4, {DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, 1, 256 * 4}};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(list.Get(), t.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    b.textures.push_back(t);
  }
  std::vector<UINT16> grid;
  for (UINT y = 0; y < GRID; y++)
    for (UINT x = 0; x < GRID; x++) {
      UINT16 v = UINT16(y * (GRID + 1) + x), r = v + 1, d = UINT16(v + GRID + 1), e = d + 1;
      for (UINT16 i : {v, r, d, r, e, d})
        grid.push_back(i);
    }
  b.indices = buffer(device, D3D12_HEAP_TYPE_UPLOAD, grid.size() * 2, D3D12_RESOURCE_STATE_GENERIC_READ);
  void *mapped;
  CHECK(b.indices->Map(0, nullptr, &mapped));
  memcpy(mapped, grid.data(), grid.size() * 2);
  b.indirect_args = buffer(device, D3D12_HEAP_TYPE_UPLOAD, 16 * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), D3D12_RESOURCE_STATE_GENERIC_READ);
  D3D12_DRAW_INDEXED_ARGUMENTS *args;
  CHECK(b.indirect_args->Map(0, nullptr, (void **)&args));
  for (UINT i = 0; i < 16; i++)
    args[i] = {UINT(grid.size()), 1, 0, 0, 0};
  auto view = [&](UINT at, ID3D12Resource *res) { device->CreateShaderResourceView(res, nullptr, b.cpu_view(at)); };
  for (UINT i = 0; i < 48; i++)
    view(i, b.textures[i % 4].Get());
  for (UINT i = 0; i < 4; i++)
    view(16 + i, b.gbuffer[i].Get());
  view(32, b.hdr.Get());
  device->CreateUnorderedAccessView(b.filtered.Get(), nullptr, nullptr, b.cpu_view(48));
  CHECK(submit(device, b.queue.Get(), list.Get()));

  auto d = defines();
  auto vs = b.compiler.compile(hlsl, "vs_grid", "vs", d), full = b.compiler.compile(hlsl, "vs_full", "vs", d);
  auto ps = b.compiler.compile(hlsl, "ps_gbuffer", "ps", d), light = b.compiler.compile(hlsl, "ps_light", "ps", d);
  auto flat = b.compiler.compile(hlsl, "ps_flat", "ps", d), filter = b.compiler.compile(hlsl, "cs_filter", "cs", d);
  if (vs.empty() || full.empty() || ps.empty() || light.empty() || flat.empty() || filter.empty()) {
    printf("failed: HLSL did not compile\n");
    ExitProcess(1);
  }
  const D3D12_CULL_MODE culls[4] = {D3D12_CULL_MODE_NONE, D3D12_CULL_MODE_BACK, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_NONE};
  for (UINT i = 0; i < 4; i++)
    b.pso_gbuffer[i] = graphics(b, vs, ps, 4, color, true, culls[i]);
  b.pso_light = graphics(b, full, light, 1, color, false, D3D12_CULL_MODE_NONE);
  b.pso_flat = graphics(b, vs, flat, 1, color, false, D3D12_CULL_MODE_NONE);
  D3D12_COMPUTE_PIPELINE_STATE_DESC cd{b.rs.Get(), bytecode(filter)};
  CHECK(device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&b.pso_filter)));

  D3D12_QUERY_HEAP_DESC qh{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kTimestamps};
  CHECK(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&b.query_heap)));
  b.stamps = buffer(device, D3D12_HEAP_TYPE_READBACK, kTimestamps * 8, D3D12_RESOURCE_STATE_COPY_DEST);
  b.readback = buffer(device, D3D12_HEAP_TYPE_READBACK, UINT64(W) * H * 8 + 65536, D3D12_RESOURCE_STATE_COPY_DEST);
  for (UINT i = 0; i < kAllocators; i++) {
    ComPtr<ID3D12CommandAllocator> a;
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)));
    b.allocators.push_back(a);
  }
  for (UINT i = 0; i < kLists; i++) {
    ComPtr<ID3D12GraphicsCommandList> l;
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, b.allocators[i].Get(), nullptr, IID_PPV_ARGS(&l)));
    CHECK(l->Close());
    b.lists.push_back(l);
  }
  b.wait();
}

static void bind(Bench &b, ID3D12GraphicsCommandList *l, UINT w = W, UINT h = H) {
  ID3D12DescriptorHeap *heaps[] = {b.views.Get()};
  l->SetDescriptorHeaps(1, heaps);
  l->SetGraphicsRootSignature(b.rs.Get());
  l->SetComputeRootSignature(b.rs.Get());
  l->SetGraphicsRootDescriptorTable(1, b.gpu_view(0));
  l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_INDEX_BUFFER_VIEW ib{b.indices->GetGPUVirtualAddress(), UINT(GRID * GRID * 6 * 2), DXGI_FORMAT_R16_UINT};
  l->IASetIndexBuffer(&ib);
  D3D12_VIEWPORT viewport{0, 0, FLOAT(w), FLOAT(h), 0, 1};
  D3D12_RECT scissor{0, 0, LONG(w), LONG(h)};
  l->RSSetViewports(1, &viewport);
  l->RSSetScissorRects(1, &scissor);
}

static void constants(ID3D12GraphicsCommandList *l, UINT i) {
  float c[8] = {float(i % 37) / 18.5f - 1, float(i % 23) / 11.5f - 1, i * 0.01f, i * 0.02f, 1, 0.8f, 0.6f, 1};
  l->SetGraphicsRoot32BitConstants(0, 8, c, 0);
}

static void stamp(Bench &b, ID3D12GraphicsCommandList *l) {
  if (b.stamp < kTimestamps)
    l->EndQuery(b.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, b.stamp++);
}

static void target_gbuffer(Bench &b, ID3D12GraphicsCommandList *l, bool to_read) {
  for (auto &g : b.gbuffer) {
    auto from = to_read ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    auto to = to_read ? D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    transition(l, g.Get(), from, to);
  }
}

// opaque geometry into four targets and depth, its pipeline changing every 20 draws
static void gbuffer_pass(Bench &b, ID3D12GraphicsCommandList *l, UINT draws) {
  auto dsv = b.dsvs->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE targets[4] = {b.rtv(0), b.rtv(1), b.rtv(2), b.rtv(3)};
  const FLOAT black[4] = {};
  for (auto &t : targets)
    l->ClearRenderTargetView(t, black, 0, nullptr);
  l->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
  l->OMSetRenderTargets(4, targets, FALSE, &dsv);
  for (UINT i = 0; i < draws; i++) {
    if (i % 20 == 0)
      l->SetPipelineState(b.pso_gbuffer[(i / 20) % 4].Get());
    constants(l, i);
    l->DrawIndexedInstanced(GRID * GRID * 6, 1, 0, 0, 0);
  }
}

// a full-screen shader that loops, the GPU's pixel cost
static void light_pass(Bench &b, ID3D12GraphicsCommandList *l) {
  target_gbuffer(b, l, true);
  auto t = b.rtv(4);
  l->OMSetRenderTargets(1, &t, FALSE, nullptr);
  l->SetPipelineState(b.pso_light.Get());
  l->SetGraphicsRootDescriptorTable(1, b.gpu_view(16));
  l->DrawInstanced(3, 1, 0, 0);
  l->SetGraphicsRootDescriptorTable(1, b.gpu_view(0));
  target_gbuffer(b, l, false);
}

// many small passes: each target switch, with a clear between, ends a Metal pass
static void small_passes(Bench &b, ID3D12GraphicsCommandList *l, UINT passes) {
  l->SetPipelineState(b.pso_flat.Get());
  D3D12_RESOURCE_BARRIER aliased{D3D12_RESOURCE_BARRIER_TYPE_UAV};
  aliased.UAV.pResource = nullptr;
  auto t = b.rtv(5);
  const FLOAT clear[4] = {0.1f, 0.2f, 0.3f, 1};
  for (UINT i = 0; i < passes; i++) {
    l->ClearRenderTargetView(t, clear, 0, nullptr);
    l->OMSetRenderTargets(1, &t, FALSE, nullptr);
    constants(l, i);
    l->DrawIndexedInstanced(GRID * 6, 1, 0, 0, 0);
    l->ResourceBarrier(1, &aliased);
  }
}

static void compute_passes(Bench &b, ID3D12GraphicsCommandList *l, UINT dispatches) {
  transition(l, b.hdr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  l->SetPipelineState(b.pso_filter.Get());
  l->SetComputeRootDescriptorTable(1, b.gpu_view(32));
  l->SetComputeRootDescriptorTable(2, b.gpu_view(48));
  D3D12_RESOURCE_BARRIER uav{D3D12_RESOURCE_BARRIER_TYPE_UAV};
  uav.UAV.pResource = b.filtered.Get();
  for (UINT i = 0; i < dispatches; i++) {
    l->Dispatch(W / 8 / 16, H / 8 / 16, 1);
    l->ResourceBarrier(1, &uav);
  }
  transition(l, b.hdr.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

// ExecuteIndirect of a few draws each, as a GPU-driven renderer calls it, in varied sizes
static void indirect_calls(Bench &b, ID3D12GraphicsCommandList *l, UINT calls) {
  auto t = b.rtv(5);
  l->OMSetRenderTargets(1, &t, FALSE, nullptr);
  l->SetPipelineState(b.pso_flat.Get());
  constants(l, 1);
  D3D12_VIEWPORT viewport{0, 0, 512, 512, 0, 1};
  D3D12_RECT scissor{0, 0, 512, 512};
  l->RSSetViewports(1, &viewport);
  l->RSSetScissorRects(1, &scissor);
  for (UINT i = 0; i < calls; i++)
    l->ExecuteIndirect(b.draw_indirect.Get(), 1 + i % 16, b.indirect_args.Get(), 0, nullptr, 0);
}

using Work = std::function<void(Bench &, ID3D12GraphicsCommandList *, UINT list, UINT frame)>;

// each scene's work for one of a frame's lists; a real renderer's allocators come back to it in no fixed order
static const std::map<std::string, Work> scenes = {
    {"frame", [](Bench &b, ID3D12GraphicsCommandList *l, UINT list, UINT) {
       bind(b, l);
       switch (list) {
       case 0: gbuffer_pass(b, l, 650); break;
       case 1: gbuffer_pass(b, l, 450); break;
       case 2: light_pass(b, l); break;
       case 3: compute_passes(b, l, 40); break;
       case 4: bind(b, l, 512, 512); small_passes(b, l, 100); break;
       case 5: bind(b, l, 512, 512); small_passes(b, l, 100); break;
       case 6: indirect_calls(b, l, 90); break;
       default: indirect_calls(b, l, 88); break;
       }
     }},
    {"geometry", [](Bench &b, ID3D12GraphicsCommandList *l, UINT list, UINT) {
       bind(b, l);
       if (list == 0) gbuffer_pass(b, l, 4000);
     }},
    {"pixels", [](Bench &b, ID3D12GraphicsCommandList *l, UINT list, UINT) {
       bind(b, l);
       if (list < 4) light_pass(b, l);
     }},
    {"compute", [](Bench &b, ID3D12GraphicsCommandList *l, UINT list, UINT) {
       bind(b, l);
       if (list == 0) compute_passes(b, l, 256);
     }},
    {"passes", [](Bench &b, ID3D12GraphicsCommandList *l, UINT, UINT) {
       bind(b, l, 512, 512);
       small_passes(b, l, 125);
     }},
    {"indirect", [](Bench &b, ID3D12GraphicsCommandList *l, UINT, UINT) {
       bind(b, l);
       indirect_calls(b, l, 250);
     }},
};

// pipelines the driver has not seen: their making is the hitch of a first run
static int pipelines(Bench &b, UINT frames) {
  const UINT salt = GetTickCount();
  double total = 0;
  auto vs = b.compiler.compile(hlsl, "vs_grid", "vs", defines());
  for (UINT i = 0; i < frames; i++) {
    auto ps = b.compiler.compile(hlsl, "ps_gbuffer", "ps", defines(salt + i));
    double t = now_ms();
    auto pso = graphics(b, vs, ps, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, true, D3D12_CULL_MODE_BACK);
    double spent = now_ms() - t;
    total += spent;
    printf("frame %u %.4f 0 0 0\n", i, spent);
  }
  printf("result pipelines 0 %.3f\n", total);
  return 0;
}

int
main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[2], "list")) {
    for (auto &[name, work] : scenes)
      printf("%s\n", name.c_str());
    printf("pipelines\n");
    return 0;
  }
  if (argc < 5) {
    printf("usage: d3d12_render_bench <dxbc|dxil> <scene> <frames> <warmup>\n");
    return 2;
  }
  setvbuf(stdout, nullptr, _IOLBF, 4096);
  Bench b;
  if (!front_end(argc, argv, b.compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  double started = now_ms();
  setup(b);
  double setup_ms = now_ms() - started;
  const std::string name = argv[2];
  const UINT frames = atoi(argv[3]), warmup = atoi(argv[4]);
  if (name == "pipelines")
    return pipelines(b, frames);
  auto scene = scenes.find(name);
  if (scene == scenes.end()) {
    printf("no scene %s\n", name.c_str());
    return 2;
  }
  std::uniform_int_distribution<UINT> pick(0, kAllocators - 1);
  for (UINT f = 0; f < warmup + frames; f++) {
    double t0 = now_ms();
    b.stamp = 0;
    std::vector<ID3D12CommandList *> submitted;
    std::vector<UINT> used;
    for (UINT i = 0; i < kLists; i++) {
      UINT at;
      do at = pick(b.random); while (std::find(used.begin(), used.end(), at) != used.end());
      used.push_back(at);
      auto allocator = b.allocators[at].Get();
      CHECK(allocator->Reset());
      auto l = b.lists[i].Get();
      CHECK(l->Reset(allocator, nullptr));
      stamp(b, l);
      scene->second(b, l, i, f);
      stamp(b, l);
      if (i == kLists - 1)
        l->ResolveQueryData(b.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, b.stamp, b.stamps.Get(), 0);
      CHECK(l->Close());
      submitted.push_back(l);
    }
    double t1 = now_ms();
    b.queue->ExecuteCommandLists(UINT(submitted.size()), submitted.data());
    double t2 = now_ms();
    // nothing presents, so this is what ends a frame for DXMT_DIAG=profile
    b.queue->SetMarker(1, "dxmt.frame", sizeof("dxmt.frame"));
    b.wait();
    double t3 = now_ms();
    UINT64 *ticks;
    CHECK(b.stamps->Map(0, nullptr, (void **)&ticks));
    // a list with no work has no time of its own to give: only those that report one count
    UINT64 first = ~0ull, last = 0;
    for (UINT i = 0; i < b.stamp; i++)
      if (ticks[i]) {
        first = std::min(first, ticks[i]);
        last = std::max(last, ticks[i]);
      }
    double gpu = last > first ? (last - first) / b.frequency * 1000 : 0;
    D3D12_RANGE none{};
    b.stamps->Unmap(0, &none);
    if (f >= warmup)
      printf("frame %u %.4f %.4f %.4f %.4f\n", f - warmup, t1 - t0, t2 - t1, t3 - t2, gpu);
  }
  // the final image, for a hash that says whether the output changed
  auto a = b.allocators[0].Get();
  CHECK(a->Reset());
  auto l = b.lists[0].Get();
  CHECK(l->Reset(a, nullptr));
  ID3D12Resource *image = name == "compute" ? b.filtered.Get() : name == "passes" || name == "indirect" ? b.small_target.Get() : name == "geometry" ? b.gbuffer[0].Get() : b.hdr.Get();
  auto state = image == b.filtered.Get() ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET;
  transition(l, image, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto desc = image->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot;
  UINT64 bytes;
  b.device->GetCopyableFootprints(&desc, 0, 1, 0, &foot, nullptr, nullptr, &bytes);
  D3D12_TEXTURE_COPY_LOCATION to{b.readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}, from{image, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  to.PlacedFootprint = foot;
  l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  transition(l, image, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
  CHECK(l->Close());
  ID3D12CommandList *one[] = {l};
  b.queue->ExecuteCommandLists(1, one);
  b.wait();
  unsigned char *data;
  CHECK(b.readback->Map(0, nullptr, (void **)&data));
  UINT64 hash = 14695981039346656037ull;
  for (UINT64 i = 0; i < bytes; i++)
    hash = (hash ^ data[i]) * 1099511628211ull;
  printf("result %s %016llx %.3f\n", name.c_str(), (unsigned long long)hash, setup_ms);
  return 0;
}
