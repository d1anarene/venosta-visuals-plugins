// Parameter description shared by every plugin and every host adapter (Premiere Pro, OpenFX).
// A plugin lists its controls once (ParamSpec, in panel order); the adapters create the host
// parameters from it. Host index = position in the list + 1 (index 0 is the input layer).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vv {

enum class ParamKind { GroupStart, GroupEnd, Checkbox, Float, Popup, IntSlider };

struct ParamSpec {
  int key;         // plugin's own identifier (enum value)
  ParamKind kind;
  const char* name;
  int diskId;      // stable ID saved in projects: append only, never renumber or reuse
  double validMin = 0, validMax = 0, sliderMin = 0, sliderMax = 0, dflt = 0;
  int precision = 2;            // decimal places shown (1 or 2)
  bool percent = false;         // display as percent
  const char* items = nullptr;  // popup items, '|' separated
  int itemCount = 0;
  bool supervise = false;       // host notifies the effect when the user changes it (presets)
  bool cannotTimeVary = false;  // not keyframeable
  bool startCollapsed = false;  // groups
};

/** Host-neutral parameter access by host index: numbers, checkboxes, 1-based popup values. */
struct ParamReader {
  virtual ~ParamReader() = default;
  virtual double number(int index) const = 0;
  virtual bool flag(int index) const = 0;
  virtual int popup(int index) const = 0;  // 1-based
};

/** Host-neutral parameter writing (used when an effect reacts to a change, e.g. presets). */
struct ParamWriter {
  virtual ~ParamWriter() = default;
  virtual void setNumber(int index, double value) = 0;
  virtual void setFlag(int index, bool value) = 0;
  virtual void setPopup(int index, int value1Based) = 0;
};

/** Host index (1-based) of `key` in `specs`, or -1. */
inline int hostIndex(const std::vector<ParamSpec>& specs, int key) {
  for (size_t i = 0; i < specs.size(); ++i)
    if (specs[i].key == key) return static_cast<int>(i) + 1;
  return -1;
}

inline std::vector<std::string> splitItems(const char* items) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* c = items; c && *c; ++c) {
    if (*c == '|') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += *c;
    }
  }
  out.push_back(cur);
  return out;
}

/** Stable per-frame key from a host time and frame duration (same units). Exact frame index when
 *  the time is frame-aligned; otherwise a distinct key derived from both values, so fractional or
 *  remapped times never collide with neighbouring frames. */
inline int64_t frameKeyFromTime(int64_t time, int64_t frameDuration) {
  if (frameDuration > 0 && time % frameDuration == 0) return time / frameDuration;
  uint64_t h = static_cast<uint64_t>(time) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(frameDuration);
  return static_cast<int64_t>(h | (1ull << 62));  // tagged so it never equals a small frame index
}

}  // namespace vv
