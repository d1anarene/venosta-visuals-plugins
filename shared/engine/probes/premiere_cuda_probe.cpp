// Research-only Premiere probe for the GPU (CUDA, Windows/NVIDIA) path. Requests the neighbour
// frame through the documented GPU dependency API (GetFrameDependencies, PrGPUDependency_InputFrame)
// and writes what arrived into the frame (banner), so a screenshot is the whole test result.
//
// Deliberately different from the CPU probe, which disturbed Premiere 2027 on Windows merely by
// being installed: no PF_OutFlag_WIDE_TIME_INPUT and no layer checkout at all. The software path
// only passes the picture through with a banner saying the GPU path is not active. Frames are
// copied to the host with the CUDA driver API (nvcuda.dll, loaded at run time: no CUDA toolkit or
// runtime needed), converted, annotated on the CPU and copied back: slow, but only for diagnosis.
// The rendered picture in Premiere's CUDA context is a CUdeviceptr (same as our production CUDA
// renderers, which use it through the runtime API on the current context).
#include "AEConfig.h"
#include "PrSDKTypes.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "A.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHelper.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "Param_Utils.h"
#include "PrSDKAESupport.h"
#include "PrGPUFilterModule.h"
#include "PrSDKVideoSegmentProperties.h"
#include "entry.h"

#include <windows.h>

#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "probe_common.h"
#include "probe_gpu_frames.h"
#include "probe_overlay.h"
#include "vv/pixel_io.h"
#include "vv_premiere_gpu_params.h"

namespace {

constexpr const char* kName = "Temporal Probe CUDA (research)";
constexpr const char* kMatch = "VVIS Research TemporalProbeCUDA";
constexpr const char* kBuild = "CUDA BUILD 1";
enum { Input, Offset, View, Gain, ParamCount };

// ---- Minimal CUDA driver API, loaded from nvcuda.dll (declarations as in cuda.h, 64-bit) --------
typedef int CUresult;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st* CUcontext;
typedef struct CUarray_st* CUarray;
enum CUmemorytype_ { CU_MEMORYTYPE_HOST = 1, CU_MEMORYTYPE_DEVICE = 2 };
struct CUDA_MEMCPY2D {
  size_t srcXInBytes, srcY;
  int srcMemoryType;
  const void* srcHost;
  CUdeviceptr srcDevice;
  CUarray srcArray;
  size_t srcPitch;
  size_t dstXInBytes, dstY;
  int dstMemoryType;
  void* dstHost;
  CUdeviceptr dstDevice;
  CUarray dstArray;
  size_t dstPitch;
  size_t WidthInBytes, Height;
};
struct CudaDriver {
  CUresult (__stdcall* memcpy2D)(const CUDA_MEMCPY2D*) = nullptr;
  CUresult (__stdcall* ctxGetCurrent)(CUcontext*) = nullptr;
  CUresult (__stdcall* ctxSynchronize)() = nullptr;
  std::string error;
  bool load() {
    HMODULE m = LoadLibraryA("nvcuda.dll");
    if (!m) { error = "NVCUDA.DLL NOT FOUND"; return false; }
    memcpy2D = reinterpret_cast<decltype(memcpy2D)>(GetProcAddress(m, "cuMemcpy2D_v2"));
    ctxGetCurrent = reinterpret_cast<decltype(ctxGetCurrent)>(GetProcAddress(m, "cuCtxGetCurrent"));
    ctxSynchronize = reinterpret_cast<decltype(ctxSynchronize)>(GetProcAddress(m, "cuCtxSynchronize"));
    if (!memcpy2D || !ctxGetCurrent || !ctxSynchronize) { error = "CUDA DRIVER FUNCTIONS MISSING"; return false; }
    return true;
  }
  CUresult download(CUdeviceptr src, size_t pitch, void* dst, size_t widthBytes, size_t height) const {
    CUDA_MEMCPY2D c{};
    c.srcMemoryType = CU_MEMORYTYPE_DEVICE; c.srcDevice = src; c.srcPitch = pitch;
    c.dstMemoryType = CU_MEMORYTYPE_HOST; c.dstHost = dst; c.dstPitch = widthBytes;
    c.WidthInBytes = widthBytes; c.Height = height;
    return memcpy2D(&c);
  }
  CUresult upload(const void* src, size_t widthBytes, CUdeviceptr dst, size_t pitch, size_t height) const {
    CUDA_MEMCPY2D c{};
    c.srcMemoryType = CU_MEMORYTYPE_HOST; c.srcHost = src; c.srcPitch = widthBytes;
    c.dstMemoryType = CU_MEMORYTYPE_DEVICE; c.dstDevice = dst; c.dstPitch = pitch;
    c.WidthInBytes = widthBytes; c.Height = height;
    return memcpy2D(&c);
  }
  CUresult copy(CUdeviceptr src, size_t srcPitch, CUdeviceptr dst, size_t dstPitch, size_t widthBytes, size_t height) const {
    CUDA_MEMCPY2D c{};
    c.srcMemoryType = CU_MEMORYTYPE_DEVICE; c.srcDevice = src; c.srcPitch = srcPitch;
    c.dstMemoryType = CU_MEMORYTYPE_DEVICE; c.dstDevice = dst; c.dstPitch = dstPitch;
    c.WidthInBytes = widthBytes; c.Height = height;
    return memcpy2D(&c);
  }
};

std::string n(long long v) { return std::to_string(v); }

// ---- GPU path ------------------------------------------------------------------------------------
class Probe : public PrGPUFilterBase {
 public:
  prSuiteError Initialize(PrGPUFilterInstance* data) override {
    try {
      if (!data) return suiteError_Fail;
      const prSuiteError e = PrGPUFilterBase::Initialize(data);
      if (e) return e;
      // Anything but CUDA: decline, Premiere renders the software path (banner says so).
      if (mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_CUDA) return suiteError_Fail;
      return cuda_.load() ? suiteError_NoError : suiteError_Fail;
    } catch (...) {
      return suiteError_Fail;
    }
  }

  prSuiteError GetFrameDependencies(const PrGPUFilterRenderParams* p, csSDK_int32* index,
                                    PrGPUFilterFrameDependency* out) override {
    try {
      if (!p || !index || !out || *index != 0) return suiteError_NotImplemented;  // one dependency
      PrTime requested = 0;
      if (!request(*p, offset(p), requested)) return suiteError_NotImplemented;
      *out = {};
      out->outDependencyType = PrGPUDependency_InputFrame;
      out->outTrackID = 0;
      out->outSequenceTime = requested;
      out->outNeedsFieldSeparation = false;
      ++*index;
      return suiteError_NoError;
    } catch (...) {
      return suiteError_NotImplemented;
    }
  }

  prSuiteError Render(const PrGPUFilterRenderParams* p, const PPixHand* frames, csSDK_size_t count,
                      PPixHand* output) override {
    try {
      return renderImpl(p, frames, count, output);
    } catch (...) {
      return suiteError_Fail;
    }
  }

 private:
  struct Frame {
    CUdeviceptr data = 0;
    prRect bounds{};
    csSDK_int32 rowBytes = 0;
    PrPixelFormat format = PrPixelFormat_Invalid;
    int width() const { return bounds.right - bounds.left; }
    int height() const { return bounds.bottom - bounds.top; }
  };

  int offset(const PrGPUFilterRenderParams* p) { return int(vv::prParamNumber(GetParam(Offset, p->inClipTime))); }

  bool request(const PrGPUFilterRenderParams& p, int off, PrTime& requested) {
    if (off < -3 || off > 3 || p.inRenderTicksPerFrame <= 0 ||
        p.inRenderTicksPerFrame > std::numeric_limits<PrTime>::max() / 4)
      return false;
    const PrTime delta = p.inRenderTicksPerFrame * off;
    if ((delta > 0 && p.inSequenceTime > std::numeric_limits<PrTime>::max() - delta) ||
        (delta < 0 && p.inSequenceTime < std::numeric_limits<PrTime>::min() - delta))
      return false;
    requested = p.inSequenceTime + delta;
    return true;
  }

  bool frame(PPixHand image, Frame& f) {
    void* data = nullptr;
    if (mGPUDeviceSuite->GetGPUPPixData(image, &data) || mPPixSuite->GetBounds(image, &f.bounds) ||
        mPPixSuite->GetRowBytes(image, &f.rowBytes) || mPPixSuite->GetPixelFormat(image, &f.format))
      return false;
    f.data = reinterpret_cast<CUdeviceptr>(data);
    return f.data != 0 && f.width() > 0 && f.height() > 0 && f.rowBytes > 0;
  }

  bool download(const Frame& f, bool half, vv::Image& out, std::vector<std::string>& lines, const char* what) {
    const size_t wb = size_t(f.width()) * vv::probe::gpuPixelBytes(half);
    std::vector<unsigned char> host(wb * size_t(f.height()));
    const CUresult r = cuda_.download(f.data, size_t(f.rowBytes), host.data(), wb, size_t(f.height()));
    if (r) {
      lines.push_back(std::string(what) + " DOWNLOAD FAILED: CU ERR " + n(r));
      return false;
    }
    out = vv::probe::fromGpuBGRA(host.data(), wb, f.width(), f.height(), half);
    return true;
  }

  prSuiteError renderImpl(const PrGPUFilterRenderParams* p, const PPixHand* frames, csSDK_size_t count,
                          PPixHand* output) {
    if (!p || !frames || count < 1 || !output) return suiteError_Fail;
    std::lock_guard<std::mutex> lock(mutex_);  // instances may be called concurrently
    std::vector<std::string> lines;
    lines.push_back(std::string(kName) + " " + kBuild);
    lines.push_back("GPU PATH: CUDA (DEPENDENCY API)");
    const int off = offset(p);
    const int mode = int(vv::prParamNumber(GetParam(View, p->inClipTime)));
    const double gain = vv::prParamNumber(GetParam(Gain, p->inClipTime));
    const long long tpf = p->inRenderTicksPerFrame;
    lines.push_back("VIEW " + n(mode) + "  OFFSET " + n(off) + "  SEQ FRAME " + n(tpf > 0 ? p->inSequenceTime / tpf : -1) +
                    "  CLIP FRAME " + n(tpf > 0 ? p->inClipTime / tpf : -1));
    Frame cur, dst;
    if (!frame(frames[0], cur) || !frame(*output, dst)) return suiteError_Fail;
    const bool half = cur.format == PrPixelFormat_GPU_BGRA_4444_16f;
    if (!half && cur.format != PrPixelFormat_GPU_BGRA_4444_32f) return suiteError_Fail;
    lines.push_back("FRAMES DELIVERED: " + n((long long)count) + "  FORMAT " + (half ? "16F" : "32F") + "  " +
                    n(cur.width()) + "X" + n(cur.height()) + "  QUALITY " + n(p->inQuality) + "  DOWNSAMPLE " +
                    std::to_string(p->inDownsampleFactorX).substr(0, 4));
    CUcontext ctx = nullptr;
    cuda_.ctxGetCurrent(&ctx);
    if (!ctx) lines.push_back("NO CURRENT CUDA CONTEXT");
    cuda_.ctxSynchronize();  // the host's preceding work on these buffers must be finished

    vv::Image current;
    if (!download(cur, half, current, lines, "CURRENT")) {
      // Cannot annotate: at least show the input instead of leaving the output undefined.
      const size_t wb = size_t(cur.width()) * vv::probe::gpuPixelBytes(half);
      if (cur.data != dst.data) cuda_.copy(cur.data, size_t(cur.rowBytes), dst.data, size_t(dst.rowBytes), wb, size_t(cur.height()));
      return suiteError_NoError;
    }
    vv::Image result = current;
    if (count >= 2) {
      Frame nb;
      vv::Image neighbour;
      if (!frame(frames[1], nb)) {
        lines.push_back("NEIGHBOUR FRAME UNREADABLE");
      } else if (nb.format != cur.format || nb.width() != cur.width() || nb.height() != cur.height()) {
        lines.push_back("NEIGHBOUR DIFFERS: " + n(nb.width()) + "X" + n(nb.height()) + " FORMAT " + n((long long)nb.format));
      } else if (download(nb, half, neighbour, lines, "NEIGHBOUR")) {
        lines.push_back(std::string("NEIGHBOUR IDENTICAL TO CURRENT: ") +
                        (vv::probe::hash(neighbour) == vv::probe::hash(current) ? "YES" : "NO"));
        if (mode != 0) result = vv::probe::display(current, &neighbour, mode, gain);
      }
    } else {
      lines.push_back("NO NEIGHBOUR DELIVERED (ONLY THE CURRENT FRAME)");
    }
    if (mode != 0 && result.px == current.px && count < 2) lines.push_back("SHOWING CURRENT FRAME");
    if (dst.width() != result.width || dst.height() != result.height) {
      lines.push_back("OUTPUT SIZE DIFFERS: " + n(dst.width()) + "X" + n(dst.height()));
      return suiteError_Fail;
    }
    vv::probe::drawBanner(result, lines);
    const size_t wb = size_t(result.width) * vv::probe::gpuPixelBytes(half);
    std::vector<unsigned char> host(wb * size_t(result.height));
    vv::probe::toGpuBGRA(result, host.data(), wb, half);
    return cuda_.upload(host.data(), wb, dst.data, size_t(dst.rowBytes), size_t(result.height)) ? suiteError_Fail
                                                                                                 : suiteError_NoError;
  }

  CudaDriver cuda_;
  std::mutex mutex_;
};

// ---- Software path (AE API): pass-through with a banner; never checks out other frames --------
vv::PixelLayout layout(PF_InData* in_data, PF_OutData* out_data, PF_EffectWorld* world) {
  AEFX_SuiteScoper<PF_PixelFormatSuite1> suite(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
  PrPixelFormat f = PrPixelFormat_Invalid;
  if (suite->GetPixelFormat(world, &f)) throw std::runtime_error("pixel format unavailable");
  if (f == PrPixelFormat_BGRA_4444_32f) return vv::PixelLayout::BGRA_32f;
  if (f == PrPixelFormat_BGRA_4444_8u) return vv::PixelLayout::BGRA_8u;
  throw std::runtime_error("unsupported pixel format");
}

PF_Err globalSetup(PF_InData* in_data, PF_OutData* out_data) {
  out_data->my_version = PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1);
  // Same flags as the production plugins (vv_pipl.r); deliberately no WIDE_TIME_INPUT.
  out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
  out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
  if (in_data->appl_id == 'PrMr') {
    AEFX_SuiteScoper<PF_PixelFormatSuite1> suite(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
    suite->ClearSupportedPixelFormats(in_data->effect_ref);
    suite->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
    suite->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_BGRA_4444_8u);
  }
  return PF_Err_NONE;
}

PF_Err paramsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_ParamDef def;
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("Neighbour offset (frames)", -3, 3, -3, 3, -1, 1);
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("View: 0 current / 1 neighbour / 2 difference", 0, 2, 0, 2, 0, 2);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Difference gain", 0, 1000, 0, 10, 1, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, 3);
  out_data->num_params = ParamCount;
  return PF_Err_NONE;
}

PF_Err render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_EffectWorld* input = &params[Input]->u.ld;
  if (!output || !output->data || !input->data) return PF_Err_INVALID_CALLBACK;
  try {
    const vv::PixelLayout in = layout(in_data, out_data, input), out = layout(in_data, out_data, output);
    vv::Image img;
    vv::readPixels(input->data, input->rowbytes, input->width, input->height, in, img);
    if (img.width != output->width || img.height != output->height) return PF_COPY(input, output, nullptr, nullptr);
    const long long frame = in_data->time_step > 0 ? (long long)(in_data->current_time / in_data->time_step) : -1;
    vv::probe::drawBanner(img, {std::string(kName) + " " + kBuild, "CPU (SOFTWARE) PATH - GPU PROBE NOT ACTIVE",
                                "NO NEIGHBOUR FRAMES ON THIS PATH  FRAME " + n(frame)});
    vv::writePixels(img, output->data, output->rowbytes, out);
    return PF_Err_NONE;
  } catch (...) {
    return PF_COPY(input, output, nullptr, nullptr);
  }
}

}  // namespace

DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<Probe>)

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr p, PF_PluginDataCB2 cb, SPBasicSuite*, const char*,
                                                     const char*) {
  return PF_REGISTER_EFFECT_EXT2(p, cb, kName, kMatch, "Venosta Visuals Research", AE_RESERVED_INFO, "EffectMain",
                                 "https://venostavisuals.com");
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* output, void*) {
  try {
    switch (cmd) {
      case PF_Cmd_ABOUT:
        PF_SPRINTF(out_data->return_msg, "%s\rResearch-only GPU dependency probe; not a denoiser.", kName);
        return PF_Err_NONE;
      case PF_Cmd_GLOBAL_SETUP: return globalSetup(in_data, out_data);
      case PF_Cmd_PARAMS_SETUP: return paramsSetup(in_data, out_data);
      case PF_Cmd_RENDER: return render(in_data, out_data, params, output);
      default: return PF_Err_NONE;
    }
  } catch (...) {
    return PF_Err_INTERNAL_STRUCT_DAMAGED;
  }
}
