// Deterministic row-parallel loop: each row is processed by exactly one thread with no shared
// accumulation, so results are bit-identical regardless of thread count or scheduling.
#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace fe {

template <typename Fn>
void parallelRows(int rows, Fn&& fn, int minRowsPerTask = 16) {
  unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  int tasks = static_cast<int>(std::min<unsigned>(hw, static_cast<unsigned>(std::max(1, rows / minRowsPerTask))));
  if (tasks <= 1) {
    for (int y = 0; y < rows; ++y) fn(y);
    return;
  }
  std::atomic<int> next{0};
  auto worker = [&] {
    for (int y; (y = next.fetch_add(1)) < rows;) fn(y);
  };
  std::vector<std::thread> pool;
  pool.reserve(tasks - 1);
  for (int t = 1; t < tasks; ++t) pool.emplace_back(worker);
  worker();
  for (auto& th : pool) th.join();
}

}  // namespace fe
