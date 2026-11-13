// Small, lossless research interchange: RGB float PFM, bottom row first on disk.
// Does not assign a colour space, apply a transfer curve, or preserve alpha.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "vv/image.h"

namespace vv { namespace io {
inline bool littleEndian() { const uint16_t n=1; return *reinterpret_cast<const uint8_t*>(&n)==1; }
inline uint32_t swap32(uint32_t n) {
  return ((n & 255u)<<24) | ((n & 65280u)<<8) | ((n>>8)&65280u) | (n>>24);
}
inline void validatePfmSize(int w,int h) {
  if(w<=0 || h<=0 || w>16384 || h>16384 || uint64_t(w)*uint64_t(h)>33554432u)
    throw std::invalid_argument("PFM dimensions out of supported bounds");
}
inline void writePfm(const std::string& path,const Image& img) {
  validatePfmSize(img.width,img.height);
  if(img.px.size()!=size_t(img.width)*img.height*4) throw std::invalid_argument("PFM image storage mismatch");
  for(size_t i=0;i<img.px.size();++i) if(i%4!=3 && !std::isfinite(img.px[i]))
    throw std::invalid_argument("PFM cannot contain nonfinite RGB");
  std::ofstream f(path,std::ios::binary);
  if(!f) throw std::runtime_error("Cannot write PFM: "+path);
  f<<"PF\n"<<img.width<<' '<<img.height<<"\n-1.0\n";
  for(int y=img.height-1;y>=0;--y) for(int x=0;x<img.width;++x) for(int c=0;c<3;++c) {
    uint32_t bits; std::memcpy(&bits,&img.at(x,y)[c],4);
    if(!littleEndian()) bits=swap32(bits);
    f.write(reinterpret_cast<const char*>(&bits),4);
  }
  if(!f) throw std::runtime_error("Failed writing PFM: "+path);
}
inline Image readPfm(const std::string& path) {
  std::ifstream f(path,std::ios::binary);
  if(!f) throw std::runtime_error("Cannot read PFM: "+path);
  auto line=[&]() {
    std::string s;
    while(std::getline(f,s)) {
      if(!s.empty() && s.back()=='\r') s.pop_back();
      if(!s.empty() && s[0]!='#') return s;
    }
    throw std::runtime_error("Truncated PFM header: "+path);
  };
  if(line()!="PF") throw std::runtime_error("Expected RGB PFM: "+path);
  int w=0,h=0; std::string extra;
  std::istringstream dims(line());
  if(!(dims>>w>>h) || (dims>>extra)) throw std::runtime_error("Invalid PFM dimensions: "+path);
  validatePfmSize(w,h);
  float scale=0; std::istringstream scales(line());
  if(!(scales>>scale) || (scales>>extra) || !std::isfinite(scale) || std::fabs(scale)!=1.f)
    throw std::runtime_error("PFM requires unit scale: "+path);
  const auto begin=f.tellg(); f.seekg(0,std::ios::end); const auto end=f.tellg();
  if(begin<0 || end<begin || uint64_t(end-begin)!=uint64_t(w)*h*12u)
    throw std::runtime_error("PFM payload size mismatch: "+path);
  f.seekg(begin);
  Image out(w,h);
  for(int y=h-1;y>=0;--y) for(int x=0;x<w;++x) {
    for(int c=0;c<3;++c) {
      uint32_t bits=0; f.read(reinterpret_cast<char*>(&bits),4);
      if(!f) throw std::runtime_error("Truncated PFM pixels: "+path);
      if((scale<0)!=littleEndian()) bits=swap32(bits);
      std::memcpy(&out.at(x,y)[c],&bits,4);
      if(!std::isfinite(out.at(x,y)[c])) throw std::runtime_error("Nonfinite PFM pixels: "+path);
    }
    out.at(x,y)[3]=1.f;
  }
  if(f.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Unexpected trailing PFM data: "+path);
  return out;
}
// Canonical little-endian IEEE754 RGB content fingerprint, not a cryptographic provenance hash.
inline uint64_t rgbFingerprint(const Image& img) {
  uint64_t h=14695981039346656037ull;
  for(size_t i=0;i<img.px.size();++i) if(i%4!=3) {
    uint32_t v; std::memcpy(&v,&img.px[i],4);
    for(int b=0;b<4;++b) { h^=(v>>(8*b))&255u; h*=1099511628211ull; }
  }
  return h;
}
}} // namespace vv::io
