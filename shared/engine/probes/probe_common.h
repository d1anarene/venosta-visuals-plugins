// Research-only diagnostics shared by temporal host probes. No production render path changes.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include "vv/image.h"

namespace vv { namespace probe {
constexpr const char* kSchema = "vv-temporal-probe-v1";
inline uint64_t hash(const Image& image) {
  uint64_t value = UINT64_C(14695981039346656037);
  auto byte = [&](unsigned char v) { value = (value ^ v) * UINT64_C(1099511628211); };
  // Canonical little-endian words, independent of row padding and native endian.
  auto word = [&](uint32_t v) { for (int b = 0; b < 4; ++b) byte(static_cast<unsigned char>(v >> (b * 8))); };
  word(static_cast<uint32_t>(image.width)); word(static_cast<uint32_t>(image.height));
  for (float f : image.px) { uint32_t bits; std::memcpy(&bits, &f, 4); word(bits); }
  return value;
}
inline std::string hex(uint64_t v) { char s[17]; std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(v)); return s; }
inline double mean(const Image& image) {
  double sum = 0; size_t n = 0;
  for (size_t i = 0; i < image.px.size(); ++i) if (i % 4 != 3 && std::isfinite(image.px[i])) { sum += image.px[i]; ++n; }
  if (!n) throw std::runtime_error("no finite RGB samples");
  return sum / static_cast<double>(n);
}
inline Image display(const Image& current, const Image* neighbour, int mode, double gain) {
  if (mode < 0 || mode > 2 || !std::isfinite(gain) || gain < 0) throw std::invalid_argument("invalid probe mode/gain");
  if (mode != 0 && (!neighbour || neighbour->width != current.width || neighbour->height != current.height))
    throw std::invalid_argument("missing or differently sized temporal input");
  Image output = current;
  if (!mode) return output;
  for (size_t i = 0; i < output.px.size(); ++i) if (i % 4 != 3)
    output.px[i] = mode == 1 ? neighbour->px[i] : static_cast<float>(std::fabs(current.px[i] - neighbour->px[i]) * gain);
  return output;
}
// Enable explicitly via the probe's Log parameter. Defaults to stderr. Set an explicit path in
// VV_TEMPORAL_PROBE_LOG to append JSONL there; each invocation writes one bounded complete record.
inline void log(const std::string& record) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  const char* path = std::getenv("VV_TEMPORAL_PROBE_LOG");
  FILE* file = path && *path ? std::fopen(path, "ab") : stderr;
  if (!file) return;
  // Bound files at 16 MiB; never remove/truncate user files or alter rendered pixels.
  bool write = true;
  if (file != stderr) { if (std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) >= 16 * 1024 * 1024) write = false; }
  if (write && record.size() <= 8192) { std::fwrite(record.data(), 1, record.size(), file); std::fputc('\n', file); std::fflush(file); }
  if (file != stderr) std::fclose(file);
}
inline std::string imageFields(const Image& current, const Image* neighbour) {
  std::ostringstream s;
  s << "\"width\":" << current.width << ",\"height\":" << current.height
    << ",\"current_hash\":\"" << hex(hash(current)) << "\",\"neighbour_available\":" << (neighbour ? "true" : "false");
  if (neighbour) s << ",\"neighbour_width\":" << neighbour->width << ",\"neighbour_height\":" << neighbour->height
    << ",\"neighbour_hash\":\"" << hex(hash(*neighbour)) << "\"";
  return s.str();
}
} }
