// Reads effect parameters in the GPU entry points (Metal and CUDA) via PrGPUFilterBase::GetParam.
// PrParam is a tagged union (PrSDKTypes.h): the value lives in the member that matches mType.
// SDK samples read float sliders as mFloat64 (Vignette), fixed sliders as mFloat32 (ProcAmp) and
// checkboxes as mBool (CrossDissolve); reading by mType covers all of them.
//
// Popup numbering in the GPU path is not documented. The CPU path gets 1-based values (AE
// convention). The combined effect's "Preset" popup always rests on its first item, so a 0 there
// reveals 0-based numbering; for the standalone effects a value of 0 in any popup reveals it.
#pragma once
#include "FilmEffectsParams.h"
#include "PrSDKTypes.h"

namespace fe {

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

/** ParamReader over a GetParam-like callable: `PrParam get(int index)`. */
template <typename Getter>
struct GpuParamReader : ParamReader {
  Getter get;
  bool zeroBasedPopups = false;
  GpuParamReader(Getter g, Variant v) : get(g) {
    // Detect 0-based popup numbering (see header comment).
    for (ParamKey k : variantKeys(v)) {
      if (specFor(k).kind != ParamKind::Popup) continue;
      if (static_cast<int>(prParamNumber(get(paramIndex(v, k)))) == 0) {
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

}  // namespace fe
