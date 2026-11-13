// GPU frame conversion: 32f round trip exact, 16f within half precision, channel order, padding kept.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "probe_gpu_frames.h"
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); std::abort(); } } while (0)
int main() {
  for (bool half : {false, true}) {
    const int w = 7, h = 3;
    const size_t row = size_t(w) * vv::probe::gpuPixelBytes(half) + 24;  // padded rows
    std::vector<unsigned char> buf(row * h, 0xAB);
    vv::Image img(w, h);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float* p = img.at(x, y);
        p[0] = 0.1f * x; p[1] = -0.25f + y; p[2] = 1.5f; p[3] = 0.5f;
      }
    vv::probe::toGpuBGRA(img, buf.data(), row, half);
    CHECK(buf[row - 1] == 0xAB && buf[row * h - 1] == 0xAB);  // padding untouched
    if (!half) {  // stored as B, G, R, A
      float first[4];
      std::memcpy(first, buf.data(), 16);
      CHECK(first[0] == 1.5f && first[2] == 0.0f && first[3] == 0.5f);
    }
    const vv::Image back = vv::probe::fromGpuBGRA(buf.data(), row, w, h, half);
    double m = 0;
    for (size_t i = 0; i < img.px.size(); ++i) m = std::fmax(m, std::fabs(double(back.px[i]) - img.px[i]));
    CHECK(half ? m < 2e-3 : m == 0.0);
  }
  bool threw = false;
  try { vv::probe::fromGpuBGRA(nullptr, 0, 1, 1, false); } catch (const std::invalid_argument&) { threw = true; }
  CHECK(threw);
  std::printf("probe GPU frames: ok\n");
  return 0;
}
