// Research-only Premiere GPU dependency probe. Uses actual 27.0 SDK dependency API and Metal.
#import <Metal/Metal.h>
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
#include "vv_premiere_gpu_params.h"
#include "gpu/vv_gpu_pixels.h"
#include "probe_common.h"
#include <limits>

namespace {
// Input/output formats remain exactly those provided by Premiere. Separate strides are explicit.
#include "probe_metal_source.h"

struct Config { uint32_t w,h,currentStride,otherStride,dstStride,mode; float gain; };
struct Frame { id<MTLBuffer> buffer=nil; prRect bounds{}; csSDK_int32 rowBytes=0; PrPixelFormat format=PrPixelFormat_Invalid; };
// No C++ or Objective-C exception may cross the SDK callback boundary.
template <typename Fn> prSuiteError guarded(Fn&& body) {
  @autoreleasepool {
    @try {
      try { return body(); } catch (...) { return suiteError_Fail; }
    } @catch (NSException*) { return suiteError_Fail; }
  }
}
class Probe : public PrGPUFilterBase {
 public:
  prSuiteError Initialize(PrGPUFilterInstance* data) override {
    return guarded([&] { return initializeImpl(data); });
  }
  prSuiteError GetFrameDependencies(const PrGPUFilterRenderParams* p,csSDK_int32* index,PrGPUFilterFrameDependency* out) override {
    return guarded([&] { return dependenciesImpl(p,index,out); });
  }
  prSuiteError Render(const PrGPUFilterRenderParams* p,const PPixHand* images,csSDK_size_t count,PPixHand* output) override {
    return guarded([&] { return renderImpl(p,images,count,output); });
  }
 private:
  prSuiteError initializeImpl(PrGPUFilterInstance* data) {
    if(!data) return suiteError_Fail;
    const auto error=PrGPUFilterBase::Initialize(data); if(error) return error;
    if(mDeviceInfo.outDeviceFramework!=PrGPUDeviceFramework_Metal || !mDeviceInfo.outCommandQueueHandle) return suiteError_Fail;
    queue_=(__bridge id<MTLCommandQueue>)mDeviceInfo.outCommandQueueHandle;
    NSError* e=nil;
    id<MTLLibrary> library=[queue_.device newLibraryWithSource:vvTemporalProbeMetalSource options:nil error:&e];
    if(!library) return suiteError_Fail;
    p32_=[queue_.device newComputePipelineStateWithFunction:[library newFunctionWithName:@"probe32"] error:&e];
    p16_=[queue_.device newComputePipelineStateWithFunction:[library newFunctionWithName:@"probe16"] error:&e];
    return p32_ && p16_ ? suiteError_NoError : suiteError_Fail;
  }
  prSuiteError dependenciesImpl(const PrGPUFilterRenderParams* p,csSDK_int32* index,PrGPUFilterFrameDependency* out) {
    if(!p || !index || !out || *index!=0) return suiteError_NotImplemented;
    const int offset=int(vv::prParamNumber(GetParam(1,p->inClipTime)));
    PrTime requested=0; if(!request(*p,offset,requested)) return suiteError_Fail;
    *out={}; out->outDependencyType=PrGPUDependency_InputFrame; out->outTrackID=0;
    out->outSequenceTime=requested; out->outNeedsFieldSeparation=false; ++*index;
    return suiteError_NoError;
  }
  prSuiteError renderImpl(const PrGPUFilterRenderParams* p,const PPixHand* images,csSDK_size_t count,PPixHand* output) {
    if(!p || !images || count<1 || !output) return suiteError_Fail;
    const int offset=int(vv::prParamNumber(GetParam(1,p->inClipTime)));
    const int mode=int(vv::prParamNumber(GetParam(2,p->inClipTime)));
    const float gain=float(vv::prParamNumber(GetParam(3,p->inClipTime)));
    const bool logging=vv::prParamNumber(GetParam(4,p->inClipTime))!=0;
    if(mode<0 || mode>2 || !std::isfinite(gain) || gain<0 || (mode && count<2)) return suiteError_Fail;
    Frame a,b,d;
    if(!frame(images[0],a)||!frame(*output,d)) return suiteError_Fail;
    if(count>=2){if(!frame(images[1],b)) return suiteError_Fail;} else b=a;
    if(!same(a,b)||!same(a,d)) return suiteError_Fail;
    const bool half=a.format==PrPixelFormat_GPU_BGRA_4444_16f;
    if(!half && a.format!=PrPixelFormat_GPU_BGRA_4444_32f) return suiteError_Fail;
    const uint32_t bytes=half?8:16;
    const uint32_t w=a.bounds.right-a.bounds.left,h=a.bounds.bottom-a.bounds.top;
    if(!w || !h || a.rowBytes<=0 || b.rowBytes<=0 || d.rowBytes<=0 || a.rowBytes%bytes || b.rowBytes%bytes || d.rowBytes%bytes) return suiteError_Fail;
    if(size_t(a.rowBytes)<size_t(w)*bytes || size_t(b.rowBytes)<size_t(w)*bytes || size_t(d.rowBytes)<size_t(w)*bytes) return suiteError_Fail;
    if(a.buffer.length<size_t(a.rowBytes)*h || b.buffer.length<size_t(b.rowBytes)*h || d.buffer.length<size_t(d.rowBytes)*h) return suiteError_Fail;
    id<MTLCommandBuffer> cb=[queue_ commandBuffer];
    if(!cb) return suiteError_Fail;
    id<MTLBuffer> ac=nil,bc=nil;
    if(logging){
      ac=[queue_.device newBufferWithLength:size_t(a.rowBytes)*h options:MTLResourceStorageModeShared];
      bc=[queue_.device newBufferWithLength:size_t(b.rowBytes)*h options:MTLResourceStorageModeShared];
      if(!ac || !bc) return suiteError_Fail;
      id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
      if(!blit) return suiteError_Fail;
      [blit copyFromBuffer:a.buffer sourceOffset:0 toBuffer:ac destinationOffset:0 size:size_t(a.rowBytes)*h];
      [blit copyFromBuffer:b.buffer sourceOffset:0 toBuffer:bc destinationOffset:0 size:size_t(b.rowBytes)*h];
      [blit endEncoding];
    }
    Config cfg={w,h,uint32_t(a.rowBytes)/bytes,uint32_t(b.rowBytes)/bytes,uint32_t(d.rowBytes)/bytes,uint32_t(mode),gain};
    id<MTLComputeCommandEncoder> enc=[cb computeCommandEncoder];
    if(!enc) return suiteError_Fail;
    [enc setComputePipelineState:half?p16_:p32_];
    [enc setBuffer:a.buffer offset:0 atIndex:0]; [enc setBuffer:b.buffer offset:0 atIndex:1]; [enc setBuffer:d.buffer offset:0 atIndex:2];
    [enc setBytes:&cfg length:sizeof(cfg) atIndex:3];
    [enc dispatchThreads:MTLSizeMake(w,h,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
    [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if(cb.status!=MTLCommandBufferStatusCompleted) return suiteError_Fail;
    if(logging){
      vv::Image current=read(ac,a,w,h,half),neighbour=read(bc,b,w,h,half);
      PrTime requested=0; if(!request(*p,offset,requested)) return suiteError_Fail;
      std::ostringstream s; s.precision(17);
      s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"premiere-metal\",\"action\":\"render\",\"clip_time_ticks\":" << p->inClipTime
        << ",\"sequence_time_ticks\":" << p->inSequenceTime << ",\"requested_neighbour_sequence_ticks\":" << requested
        << ",\"ticks_per_frame\":" << p->inRenderTicksPerFrame << ",\"returned_time_verified\":false,\"offset_frames\":" << offset
        << ",\"delivered_frame_count\":" << count << ",\"view\":" << mode << ",\"quality\":" << p->inQuality
        << ",\"downsample_x\":" << p->inDownsampleFactorX << ",\"downsample_y\":" << p->inDownsampleFactorY
        << ",\"format\":\"" << (half?"BGRA-float16":"BGRA-float32") << "\",\"field_type\":" << p->inRenderFieldType
        << ',' << vv::probe::imageFields(current,count>=2?&neighbour:nullptr) << '}'; vv::probe::log(s.str());
    }
    return suiteError_NoError;
  }
 private:
  bool request(const PrGPUFilterRenderParams& p,int offset,PrTime& requested){
    if(offset < -3 || offset>3 || p.inRenderTicksPerFrame<=0 || p.inRenderTicksPerFrame>std::numeric_limits<PrTime>::max()/3) return false;
    const PrTime delta=p.inRenderTicksPerFrame*offset;
    if((delta>0 && p.inSequenceTime>std::numeric_limits<PrTime>::max()-delta) || (delta<0 && p.inSequenceTime<std::numeric_limits<PrTime>::min()-delta)) return false;
    requested=p.inSequenceTime+delta; return true;
  }
  bool frame(PPixHand image,Frame& f){
    void* data=nullptr;
    if(mGPUDeviceSuite->GetGPUPPixData(image,&data) || mPPixSuite->GetBounds(image,&f.bounds) || mPPixSuite->GetRowBytes(image,&f.rowBytes) || mPPixSuite->GetPixelFormat(image,&f.format)) return false;
    f.buffer=(__bridge id<MTLBuffer>)data; return f.buffer!=nil;
  }
  bool same(const Frame& a,const Frame& b){ return a.format==b.format && a.bounds.left==b.bounds.left && a.bounds.right==b.bounds.right && a.bounds.top==b.bounds.top && a.bounds.bottom==b.bounds.bottom; }
  vv::Image read(id<MTLBuffer> buffer,const Frame& f,uint32_t w,uint32_t h,bool half){
    vv::Image out(w,h);
    for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x){
      const char* row=static_cast<const char*>(buffer.contents)+size_t(y)*f.rowBytes;
      float bgra[4];
      if(half){const uint16_t* p=reinterpret_cast<const uint16_t*>(row)+x*4; for(int c=0;c<4;++c) bgra[c]=vv::gpu::halfToFloat(p[c]);}
      else std::memcpy(bgra,reinterpret_cast<const float*>(row)+x*4,16);
      float* p=out.at(x,y); p[0]=bgra[2];p[1]=bgra[1];p[2]=bgra[0];p[3]=bgra[3];
    }
    return out;
  }
  id<MTLCommandQueue> queue_=nil;
  id<MTLComputePipelineState> p32_=nil,p16_=nil;
};
}
DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<Probe>)
