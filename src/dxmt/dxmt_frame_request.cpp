#include <cstring>
#include "dxmt_frame_request.hpp"
#include "log/log.hpp"
#include "thread.hpp"
#include "util_env.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

namespace dxmt::frames {

static std::atomic_bool requested;
static std::atomic_uint32_t outstanding;
static std::mutex mutex;
static std::vector<Capture> encoded, completed;

static void
Save(const std::string &directory, Capture &capture) {
  auto pixels = static_cast<uint8_t *>(capture.pixels.get());
  const uint64_t size = uint64_t(capture.width) * capture.height * 4;
  for (uint64_t i = 0; i < size; i += 4) {
    if (capture.rgba)
      std::swap(pixels[i], pixels[i + 2]);
    pixels[i + 3] = 0xff;
  }
  const uint32_t offset = 54, file_size = uint32_t(size) + offset, width = capture.width, planes_bits = 1 | 32 << 16;
  const int32_t height = -int32_t(capture.height);
  const uint32_t info_size = 40;
  uint8_t header[offset] = {'B', 'M'};
  memcpy(header + 2, &file_size, 4);
  memcpy(header + 10, &offset, 4);
  memcpy(header + 14, &info_size, 4);
  memcpy(header + 18, &width, 4);
  memcpy(header + 22, &height, 4);
  memcpy(header + 26, &planes_bits, 4);

  auto name = directory + "/frame-" + std::to_string(capture.frame);
  auto partial = name + ".tmp";
  auto file = fopen(partial.c_str(), "wb");
  bool written = file && fwrite(header, sizeof(header), 1, file) == 1 && fwrite(pixels, size, 1, file) == 1;
  if (file)
    fclose(file);
  if (!written || !MoveFileExA(partial.c_str(), (name + ".bmp").c_str(), MOVEFILE_REPLACE_EXISTING))
    ERR("Frame request: ", name, " could not be written");
}

static void
Serve(const std::string &directory) {
  env::setThreadName("dxmt-frame-request");
  const auto request = directory + "/request";
  for (;;) {
    if (GetFileAttributesA(request.c_str()) != INVALID_FILE_ATTRIBUTES && DeleteFileA(request.c_str()))
      requested.store(true, std::memory_order_relaxed);
    std::vector<Capture> ready;
    {
      std::lock_guard lock(mutex);
      ready.swap(completed);
    }
    for (auto &capture : ready)
      Save(directory, capture);
    Sleep(100);
  }
}

bool
Requested() {
  static const bool enabled = [] {
    auto directory = env::getEnvVar("DXMT_FRAME_REQUEST");
    if (directory.empty())
      return false;
    // a path of the host
    if (directory[0] == '/')
      directory = "Z:" + directory;
    dxmt::thread([directory] { Serve(directory); }).detach();
    return true;
  }();
  return enabled && requested.exchange(false, std::memory_order_relaxed);
}

void
Add(Capture &&capture) {
  std::lock_guard lock(mutex);
  encoded.push_back(std::move(capture));
  outstanding.fetch_add(1, std::memory_order_release);
}

void
Completed(uint64_t seq) {
  if (!outstanding.load(std::memory_order_acquire))
    return;
  std::lock_guard lock(mutex);
  for (auto it = encoded.begin(); it != encoded.end();) {
    if (it->seq > seq) {
      ++it;
      continue;
    }
    completed.push_back(std::move(*it));
    it = encoded.erase(it);
    outstanding.fetch_sub(1, std::memory_order_relaxed);
  }
}

} // namespace dxmt::frames
