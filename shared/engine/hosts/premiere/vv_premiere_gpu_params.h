// Reads effect parameters in the Premiere GPU entry points (Metal and CUDA) via
// PrGPUFilterBase::GetParam. PrParam is a tagged union (PrSDKTypes.h): the value lives in the
// member that matches mType. SDK samples read float sliders as mFloat64 (Vignette), fixed sliders
// as mFloat32 (ProcAmp) and checkboxes as mBool (CrossDissolve); reading by mType covers all.
//
// Popup numbering in the GPU path is not documented. The CPU path gets 1-based values (AE
// convention); a popup reading 0 therefore reveals 0-based numbering. Plugins should have a popup
// that normally rests on its first item (e.g. a preset menu) so the detection is reliable.
#pragma once
#include "PrSDKTypes.h"
#include "vv/params.h"

namespace vv {

inline double prParamNumber(const PrParam& p) {
  switch (p.mType) {
    case kPrParamType_Int8: return p.mInt8;
    case kPrParamType_Int16: return p.mInt16;
    case kPrParamType_Int32: return p.mInt32;
    case kPrParamType_Int64: return static_cast<double>(p.mInt64);
    case kPrParamType_Float32: return p.mFloat32;
    case kPrParamType_Float64: return p.mFloat64;
    case kPrParamType_Bool: return p.mBool ? 1.0 : 0.0;
    default: return p.mFloat64;
  }
}

/** ParamReader over a GetParam-like callable: `PrParam get(int hostIndex)`. */
template <typename Getter>
struct PrGpuParamReader : ParamReader {
  Getter get;
  bool zeroBasedPopups = false;
  PrGpuParamReader(Getter g, const std::vector<ParamSpec>& specs) : get(g) {
    for (size_t i = 0; i < specs.size(); ++i) {
      if (specs[i].kind != ParamKind::Popup) continue;
      if (static_cast<int>(prParamNumber(get(static_cast<int>(i) + 1))) == 0) {
        zeroBasedPopups = true;
        break;
      }
    }
  }
  double number(int i) const override { return prParamNumber(get(i)); }
  bool flag(int i) const override { return prParamNumber(get(i)) != 0.0; }
  int popup(int i) const override {
    int v = static_cast<int>(prParamNumber(get(i)));
    return zeroBasedPopups ? v + 1 : v;
  }
};

}  // namespace vv
