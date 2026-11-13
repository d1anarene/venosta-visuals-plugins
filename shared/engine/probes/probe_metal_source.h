#pragma once
#import <Metal/Metal.h>
inline NSString* vvTemporalProbeMetalSource=@R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Config { uint w,h,currentStride,otherStride,dstStride,mode; float gain; };
kernel void probe32(device const float4* a [[buffer(0)]], device const float4* b [[buffer(1)]], device float4* d [[buffer(2)]], constant Config& p [[buffer(3)]], uint2 xy [[thread_position_in_grid]]) {
 if(xy.x>=p.w || xy.y>=p.h) return;
 float4 c=a[xy.y*p.currentStride+xy.x], n=b[xy.y*p.otherStride+xy.x];
 float4 o=p.mode==0 ? c : p.mode==1 ? n : abs(c-n)*p.gain; o.w=c.w; d[xy.y*p.dstStride+xy.x]=o;
}
kernel void probe16(device const half4* a [[buffer(0)]], device const half4* b [[buffer(1)]], device half4* d [[buffer(2)]], constant Config& p [[buffer(3)]], uint2 xy [[thread_position_in_grid]]) {
 if(xy.x>=p.w || xy.y>=p.h) return;
 half4 c=a[xy.y*p.currentStride+xy.x], n=b[xy.y*p.otherStride+xy.x];
 half4 o=p.mode==0 ? c : p.mode==1 ? n : half4(abs(float4(c)-float4(n))*p.gain); o.w=c.w; d[xy.y*p.dstStride+xy.x]=o;
}
)METAL";
