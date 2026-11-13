#include "probe_common.h"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (false)
#include <limits>
#include <cstdio>
int main() {
  vv::Image a(3, 2), b(3, 2);
  for (size_t i = 0; i < a.px.size(); ++i) { a.px[i] = float(i) / 17; b.px[i] = float(i) / 19; }
  const auto original = vv::probe::hash(a);
  CHECK(vv::probe::display(a, nullptr, 0, 1).px == a.px);
  auto n = vv::probe::display(a, &b, 1, 1), d = vv::probe::display(a, &b, 2, 2);
  for (size_t i = 0; i < a.px.size(); ++i) {
    CHECK(n.px[i] == (i % 4 == 3 ? a.px[i] : b.px[i]));
    if (i % 4 == 3) CHECK(d.px[i] == a.px[i]); else CHECK(d.px[i] == std::fabs(a.px[i]-b.px[i])*2);
  }
  CHECK(vv::probe::hash(a) == original && vv::probe::hash(b) != original);
  bool failed = false; try { vv::probe::display(a, nullptr, 1, 1); } catch (const std::invalid_argument&) { failed = true; }
  CHECK(failed);
  vv::Image small(1,1); failed = false; try { vv::probe::display(a, &small, 2, 1); } catch (const std::invalid_argument&) { failed = true; }
  CHECK(failed);
  // The same frame renders identically after arbitrary intervening requests.
  const auto expected = vv::probe::display(a, &b, 2, 2);
  vv::probe::display(b, &a, 1, 1);
  CHECK(vv::probe::display(a, &b, 2, 2).px == expected.px);
  CHECK(vv::probe::imageFields(a, nullptr).find("\"neighbour_available\":false") != std::string::npos);
  std::puts("probe common: identity, neighbour, difference, alpha, hashes, missing input and order checks pass");
}
