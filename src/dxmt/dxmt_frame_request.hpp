#pragma once
#include "Metal.hpp"
#include "wsi_platform.hpp"
#include <cstdint>
#include <memory>

namespace dxmt::frames {

struct Capture {
  std::unique_ptr<void, void (*)(void *)> pixels{nullptr, wsi::aligned_free};
  WMT::Reference<WMT::Buffer> buffer;
  uint32_t width = 0, height = 0;
  bool rgba = false;
  uint64_t frame = 0, seq = 0;
};

// DXMT_FRAME_REQUEST=<dir>: creating <dir>/request saves the next presented frame as <dir>/frame-<number>.bmp
bool Requested();
void Add(Capture &&capture);
void Completed(uint64_t seq);

} // namespace dxmt::frames
