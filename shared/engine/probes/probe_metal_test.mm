// Real Metal kernel comparison. Not an Adobe host or temporal frame-access test.
#import <Metal/Metal.h>
#include "probe_metal_source.h"
#include "probe_common.h"
#include "gpu/vv_gpu_pixels.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(false)
struct Config { uint32_t w,h,currentStride,otherStride,dstStride,mode; float gain; };
int main(){ @autoreleasepool {
  id<MTLDevice> device=MTLCreateSystemDefaultDevice();
  if(!device){std::puts("SKIP: no Metal device; GPU pixels unverified");return 77;}
  NSError* error=nil;
  id<MTLLibrary> library=[device newLibraryWithSource:vvTemporalProbeMetalSource options:nil error:&error];
  if(!library){std::fprintf(stderr,"Metal shader compile: %s\n",error.localizedDescription.UTF8String);return 1;}
  id<MTLCommandQueue> queue=[device newCommandQueue];
  int cases=0;
  for(bool half : {false,true}) for(uint32_t mode : {0u,1u,2u}){
    const uint32_t w=31,h=17,sa=37,sb=41,sd=43,bytes=half?8:16;
    id<MTLBuffer> a=[device newBufferWithLength:sa*h*bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> b=[device newBufferWithLength:sb*h*bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> d=[device newBufferWithLength:sd*h*bytes options:MTLResourceStorageModeShared];
    CHECK(a&&b&&d);
    std::memset(d.contents,0x6d,d.length);
    vv::Image ca(w,h),cb(w,h);
    auto fill=[&](id<MTLBuffer> buffer,uint32_t stride,vv::Image& image,int variant){
      for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x){
        float rgba[4]={float(int(x)-15)*0.13f+variant*0.1f,float(y)*0.17f,float(x+y)*0.03f,0.3f+float(x%5)*0.1f};
        for(int c=0;c<4;++c){
          const int bgra=c==0?2:c==2?0:c; float value=rgba[c];
          if(half){value=vv::gpu::halfToFloat(vv::gpu::floatToHalf(value));static_cast<uint16_t*>(buffer.contents)[(y*stride+x)*4+bgra]=vv::gpu::floatToHalf(value);}
          else static_cast<float*>(buffer.contents)[(y*stride+x)*4+bgra]=value;
          image.at(x,y)[c]=value;
        }
      }
    };
    fill(a,sa,ca,0);fill(b,sb,cb,1);
    id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:half?@"probe16":@"probe32"] error:&error];CHECK(pipeline);
    id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLComputeCommandEncoder> enc=[command computeCommandEncoder];
    Config cfg={w,h,sa,sb,sd,mode,2};
    [enc setComputePipelineState:pipeline];[enc setBuffer:a offset:0 atIndex:0];[enc setBuffer:b offset:0 atIndex:1];[enc setBuffer:d offset:0 atIndex:2];[enc setBytes:&cfg length:sizeof(cfg) atIndex:3];
    [enc dispatchThreads:MTLSizeMake(w,h,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];[enc endEncoding];[command commit];[command waitUntilCompleted];CHECK(command.status==MTLCommandBufferStatusCompleted);
    const vv::Image expected=vv::probe::display(ca,&cb,mode,2);
    for(uint32_t y=0;y<h;++y){
      for(uint32_t x=0;x<w;++x) for(int c=0;c<4;++c){
        const int bgra=c==0?2:c==2?0:c;
        const float value=half?vv::gpu::halfToFloat(static_cast<const uint16_t*>(d.contents)[(y*sd+x)*4+bgra]):static_cast<const float*>(d.contents)[(y*sd+x)*4+bgra];
        const float target=half?vv::gpu::halfToFloat(vv::gpu::floatToHalf(expected.at(x,y)[c])):expected.at(x,y)[c];
        CHECK(std::fabs(value-target)<(half?0.002f:1e-6f));
      }
      for(size_t k=size_t(y*sd+w)*bytes;k<size_t((y+1)*sd)*bytes;++k)CHECK(static_cast<const unsigned char*>(d.contents)[k]==0x6d);
    }
    ++cases;
  }
  std::printf("Metal probe pixels: %d float32/float16 cases pass (modes, odd dimensions, independent padded strides, alpha, extended range)\n",cases);
  return 0;
}}
