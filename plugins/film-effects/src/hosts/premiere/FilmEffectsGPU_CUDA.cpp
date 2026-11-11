// Film Effects — Premiere Pro GPU entry point for CUDA (Windows / NVIDIA), on top of the Premiere
// SDK's GPU filter extension (xGPUFilterEntry / PrGPUFilter). Counterpart of FilmEffectsGPU.mm
// (Metal); both call an engine that shares its maths with the tested CPU reference.
//
// STATUS: DRAFT, NOT YET COMPILED against the Premiere SDK. Documented facts used (Premiere SDK
// guide, gpu-effects-transitions/*): GPU entry supplements the software effect; CreateInstance
// returning an error makes Premiere render in software; Render may work in place; result keeps
// the input pixel format; GetParam() reads parameters; CUDA via the runtime API is supported when
// the runtime is linked statically ("Statically Link to CUDA Runtime").
// Helper names from the SDK_ProcAmp GPU sample (PrGPUFilterBase, mDeviceInfo, mGPUDeviceSuite,
// mPPixSuite, DECLARE_GPUFILTER_ENTRY, PrGPUDeviceFramework_CUDA) must be checked against the SDK
// you build with — this file and FilmEffectsGPU.mm are the only places they are used.

#include "PrGPUFilterModule.h"
#include "PrSDKVideoSegmentProperties.h"

#include <memory>
#include <mutex>

#include "CudaFilmEngine.h"
#include "FilmEffects.h"
#include "FilmEffectsParams.h"

namespace {

class FilmEffectsGPU : public PrGPUFilterBase {
 public:
  prSuiteError Initialize(PrGPUFilterInstance* ioInstanceData) override {
    PrGPUFilterBase::Initialize(ioInstanceData);
    // CUDA only here. Returning an error falls back to the software path (documented behaviour).
    if (mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_CUDA) return suiteError_Fail;
    if (!mDeviceInfo.outMeetsMinimumRequirementsForAcceleration) return suiteError_Fail;
    mEngine = std::make_unique<fe::CudaFilmEngine>();
    if (!mEngine->ok()) return suiteError_Fail;
    return suiteError_NoError;
  }

  prSuiteError Render(const PrGPUFilterRenderParams* inRenderParams, const PPixHand* inFrames,
                      csSDK_size_t inFrameCount, PPixHand* outFrame) override {
    if (inFrameCount < 1 || !mEngine) return suiteError_Fail;

    struct Reader : fe::ParamReader {
      FilmEffectsGPU* self;
      PrTime t;
      double number(int i) const override { return self->GetParam(i, t).mFloat64; }
      bool flag(int i) const override { return self->GetParam(i, t).mBool != 0; }
      int popup(int i) const override { return self->GetParam(i, t).mInt32; }
    } reader;
    reader.self = this;
    reader.t = inRenderParams->inClipTime;
    const fe::FilmParams params = fe::readParams(reader, static_cast<fe::Variant>(FE_VARIANT));

    void* srcData = nullptr;
    void* dstData = nullptr;
    mGPUDeviceSuite->GetGPUPPixData(inFrames[0], &srcData);
    mGPUDeviceSuite->GetGPUPPixData(*outFrame, &dstData);
    PrPixelFormat format = PrPixelFormat_Invalid;
    mPPixSuite->GetPixelFormat(*outFrame, &format);
    prRect bounds = {};
    mPPixSuite->GetBounds(*outFrame, &bounds);
    csSDK_int32 rowBytes = 0;
    mPPixSuite->GetRowBytes(*outFrame, &rowBytes);

    const bool half = format == PrPixelFormat_GPU_BGRA_4444_16f;
    if (!half && format != PrPixelFormat_GPU_BGRA_4444_32f) return suiteError_Fail;
    const uint32_t bytesPerPixel = half ? 8 : 16;
    const uint32_t width = uint32_t(bounds.right - bounds.left);
    const uint32_t height = uint32_t(bounds.bottom - bounds.top);
    const int64_t key = fe::frameKeyFromTime(inRenderParams->inClipTime, inRenderParams->inRenderTicksPerFrame);

    std::lock_guard<std::mutex> lock(mMutex);  // instances may be called concurrently
    const bool ok = mEngine->render(srcData, dstData, half, width, height, uint32_t(rowBytes) / bytesPerPixel, params, key);
    return ok ? suiteError_NoError : suiteError_Fail;
  }

 private:
  std::unique_ptr<fe::CudaFilmEngine> mEngine;
  std::mutex mMutex;
};

}  // namespace

DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<FilmEffectsGPU>)
