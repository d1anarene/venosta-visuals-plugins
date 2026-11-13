// Overlay: text lands inside an opaque box, nothing outside it changes, tiny frames do not crash.
#include <cstdio>
#include <cstdlib>
#include "probe_overlay.h"
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); std::abort(); } } while (0)
int main() {
  vv::Image im(640, 360);
  for (size_t i = 0; i < im.px.size(); ++i) im.px[i] = (i % 4 == 3) ? 0.0f : 0.5f;
  vv::probe::drawBanner(im, {"VIEW 1 OFFSET -1", "ERR 0 DATA YES"});
  int white = 0;
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 120; ++x) white += im.at(x, y)[0] == 1.0f;
  CHECK(white > 50);
  CHECK(im.at(0, 0)[3] == 1.0f);                                   // opaque box
  CHECK(im.at(600, 300)[0] == 0.5f && im.at(600, 300)[3] == 0.0f);  // untouched outside
  vv::Image tiny(3, 2);
  vv::probe::drawBanner(tiny, {"A VERY LONG LINE THAT DOES NOT FIT"});
  std::printf("probe overlay: ok\n");
  return 0;
}
