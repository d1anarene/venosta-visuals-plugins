// Shared Premiere Pro GPU entry point (xGPUFilterEntry / PrGPUFilter) for any vv::Effect, on top of
// the SDK helper PrGPUFilterModule.h (names follow the SDK_ProcAmp GPU sample: PrGPUFilterBase,
// mDeviceInfo, mGPUDeviceSuite, mPPixSuite). Included by vv_premiere_gpu_metal.mm (Metal) and
// vv_premiere_gpu_cuda.cpp (CUDA) with VV_PR_GPU_FRAMEWORK / VV_PR_GPU_API set.
//
// Documented facts used (Premiere SDK guide, gpu-effects-transitions): the GPU entry supplements
// the software effect; CreateInstance/Initialize returning an error makes Premiere render that
// instance in software; Render may work in place; the result keeps the input pixel format.

// Same include order as the SDK_ProcAmp sample: AE headers before the GPU module.
#include "AEConfig.h"
#include "PrSDKTypes.h"
#include "AE_Effect.h"
#include "A.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "Param_Utils.h"
#include "PrSDKAESupport.h"
#include "PrGPUFilterModule.h"
#include "PrSDKVideoSegmentProperties.h"

#include <memory>
#include <mutex>

#include "vv/effect.h"
#include "vv_premiere_gpu_params.h"

namespace {

class VVGpuFilter : public PrGPUFilterBase {
 public:
  prSuiteError Initialize(PrGPUFilterInstance* ioInstanceData) override {
    PrGPUFilterBase::Initialize(ioInstanceData);
    if (mDeviceInfo.outDeviceFramework != VV_PR_GPU_FRAMEWORK) return suiteError_Fail;
    if (!mDeviceInfo.outMeetsMinimumRequirementsForAcceleration) return suiteError_Fail;
    if (!vv::effect().hasGpu(VV_PR_GPU_API)) return suiteError_Fail;
    mRenderer = vv::effect().makeGpuRenderer(VV_PR_GPU_API);
    if (!mRenderer || !mRenderer->prepare(mDeviceInfo.outCommandQueueHandle)) {
      mRenderer.reset();
      return suiteError_Fail;  // error -> Premiere renders this instance in software
    }
    return suiteError_NoError;
  }

  prSuiteError Render(const PrGPUFilterRenderParams* inRenderParams, const PPixHand* inFrames,
                      csSDK_size_t inFrameCount, PPixHand* outFrame) override {
    if (inFrameCount < 1 || !mRenderer) return suiteError_Fail;
    const PrTime t = inRenderParams->inClipTime;
    auto get = [this, t](int index) { return GetParam(index, t); };
    const vv::PrGpuParamReader<decltype(get)> reader(get, vv::effect().params());

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
    vv::GpuFrame f;
    f.src = srcData;
    f.dst = dstData;
    f.half = half;
    f.width = uint32_t(bounds.right - bounds.left);
    f.height = uint32_t(bounds.bottom - bounds.top);
    f.rowPixels = uint32_t(rowBytes) / (half ? 8u : 16u);
    f.queue = mDeviceInfo.outCommandQueueHandle;  // Metal command queue (CUDA: unused, default stream)
    const int64_t key = vv::frameKeyFromTime(inRenderParams->inClipTime, inRenderParams->inRenderTicksPerFrame);

    std::lock_guard<std::mutex> lock(mMutex);  // instances may be called concurrently
    return mRenderer->render(f, reader, key) ? suiteError_NoError : suiteError_Fail;
  }

 private:
  std::unique_ptr<vv::GpuRenderer> mRenderer;
  std::mutex mMutex;
};

}  // namespace

DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<VVGpuFilter>)
