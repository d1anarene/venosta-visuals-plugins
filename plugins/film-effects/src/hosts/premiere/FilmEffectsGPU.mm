// Film Effects — Premiere Pro GPU entry point (Metal), built on the Premiere SDK's GPU filter
// extension (xGPUFilterEntry / PrGPUFilter: CreateInstance, DisposeInstance, Render).
//
// STATUS: DRAFT, NOT YET COMPILED. Documented facts used (Premiere SDK guide,
// gpu-effects-transitions/*):
//   - the GPU entry supplements the software effect; matched to it by PiPL / outMatchName
//   - PrGPUFilterRenderParams: inClipTime, inRenderTicksPerFrame, inRenderWidth/Height,
//     inDownsampleFactorX/Y
//   - parameters are read with GetParam() and properties with GetProperty() from the SDK helper
//     header PrGPUFilterModule.h
//   - CreateInstance returning an error makes Premiere render that instance in software
//   - Render may operate in place on outFrame; result must keep the input pixel format
// Names of PrGPUFilterModule.h helpers (PrGPUFilterBase, mDeviceInfo, mGPUDeviceSuite,
// mPPixSuite, DECLARE_GPUFILTER_ENTRY) follow Adobe's SDK_ProcAmp GPU sample. Check them against
// the SDK you build with — this file is the only place they are used.

// Same include order as the SDK_ProcAmp sample (SDK_ProcAmp.h): AE headers before the GPU module.
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

#import <Metal/Metal.h>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <string>

#include "FilmEffects.h"
#include "FilmEffectsGPUParams.h"
#include "MetalFilmEngine.h"

namespace {

// Path of FilmEffects.metallib inside this plugin bundle (Contents/Resources/). Every variant
// bundle ships the same metallib.
std::string metallibPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&metallibPath), &info) && info.dli_fname) {
    std::string bin = info.dli_fname;  // .../<Bundle>.plugin/Contents/MacOS/<Bundle>
    auto pos = bin.rfind("/MacOS/");
    if (pos != std::string::npos) return bin.substr(0, pos) + "/Resources/FilmEffects.metallib";
  }
  return "FilmEffects.metallib";
}

class FilmEffectsGPU : public PrGPUFilterBase {
 public:
  prSuiteError Initialize(PrGPUFilterInstance* ioInstanceData) override {
    PrGPUFilterBase::Initialize(ioInstanceData);
    // Metal only. CUDA (Windows) is a later milestone; returning an error falls back to the
    // software path, which is the documented behaviour.
    if (mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_Metal) return suiteError_Fail;
    if (!mDeviceInfo.outMeetsMinimumRequirementsForAcceleration) return suiteError_Fail;
    id<MTLDevice> device = (__bridge id<MTLDevice>)mDeviceInfo.outDeviceHandle;
    mEngine = std::make_unique<fe::MetalFilmEngine>(device, metallibPath());
    if (!mEngine->ok()) return suiteError_Fail;  // e.g. metallib missing -> software fallback
    mQueue = (__bridge id<MTLCommandQueue>)mDeviceInfo.outCommandQueueHandle;
    return suiteError_NoError;
  }

  prSuiteError Render(const PrGPUFilterRenderParams* inRenderParams, const PPixHand* inFrames,
                      csSDK_size_t inFrameCount, PPixHand* outFrame) override {
    if (inFrameCount < 1 || !mEngine) return suiteError_Fail;

    const fe::Variant variant = static_cast<fe::Variant>(FE_VARIANT);
    const PrTime t = inRenderParams->inClipTime;
    auto get = [this, t](int index) { return GetParam(index, t); };
    const fe::GpuParamReader<decltype(get)> reader(get, variant);
    const fe::FilmParams params = fe::readParams(reader, variant);

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

    fe::MetalFrame src, dst;
    src.buffer = (__bridge id<MTLBuffer>)srcData;
    dst.buffer = (__bridge id<MTLBuffer>)dstData;
    src.width = dst.width = uint32_t(bounds.right - bounds.left);
    src.height = dst.height = uint32_t(bounds.bottom - bounds.top);
    src.rowPixels = dst.rowPixels = uint32_t(rowBytes) / bytesPerPixel;
    src.halfFloat = dst.halfFloat = half;

    const int64_t key = fe::frameKeyFromTime(inRenderParams->inClipTime, inRenderParams->inRenderTicksPerFrame);

    std::lock_guard<std::mutex> lock(mMutex);  // instances may be called concurrently
    id<MTLCommandBuffer> cb = [mQueue commandBuffer];
    if (!params.anyEffectActive()) {
      // Bypass: exact copy when not rendering in place.
      if (srcData != dstData) {
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:src.buffer sourceOffset:0 toBuffer:dst.buffer destinationOffset:0
                        size:size_t(rowBytes) * src.height];
        [blit endEncoding];
      }
    } else if (!mEngine->encode(cb, src, dst, params, key)) {
      return suiteError_Fail;
    }
    [cb commit];
    [cb waitUntilCompleted];  // the host expects the frame to be finished when Render returns
    return cb.status == MTLCommandBufferStatusCompleted ? suiteError_NoError : suiteError_Fail;
  }

 private:
  std::unique_ptr<fe::MetalFilmEngine> mEngine;
  id<MTLCommandQueue> mQueue = nil;
  std::mutex mMutex;
};

}  // namespace

DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<FilmEffectsGPU>)
