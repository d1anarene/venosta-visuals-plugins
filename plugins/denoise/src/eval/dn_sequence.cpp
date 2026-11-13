#include "dn_sequence.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "dn_synth.h"
#include "vv/kernels.h"
#include "vv/log_transfer.h"

namespace dn {
namespace eval {
namespace {
constexpr double kPi = 3.14159265358979323846;

struct Sample {
  float rgb[3];
  bool detail = false;
  bool flat = false;
  int detailRegion = 0;
};

int markerScale(const SequenceConfig& c) {
  return std::max(1, std::min(8, std::min(c.width / 128, c.height / 64)));
}

int markerHeight(const SequenceConfig& c) {
  return c.frameMarker ? std::min(c.height, 11 * markerScale(c)) : 0;
}

Sample frameMarker(const SequenceConfig& c, int x, int y, int index) {
  // Independently drawn 5x7 decimal digits, no external font/asset. Geometric bitmaps are
  // deliberately sharp diagnostics, never texture or temporal metric content.
  static const char* digits[10] = {
    "01110" "10001" "10011" "10101" "11001" "10001" "01110",
    "00100" "01100" "00100" "00100" "00100" "00100" "01110",
    "01110" "10001" "00001" "00010" "00100" "01000" "11111",
    "11110" "00001" "00001" "01110" "00001" "00001" "11110",
    "00010" "00110" "01010" "10010" "11111" "00010" "00010",
    "11111" "10000" "10000" "11110" "00001" "00001" "11110",
    "01110" "10000" "10000" "11110" "10001" "10001" "01110",
    "11111" "00001" "00010" "00100" "01000" "01000" "01000",
    "01110" "10001" "10001" "01110" "10001" "10001" "01110",
    "01110" "10001" "10001" "01111" "00001" "00001" "01110"
  };
  const int scale = markerScale(c), left = 2 * scale, top = 2 * scale;
  bool bright = false;
  if (x >= left && y >= top) {
    const int gx = (x - left) / scale, gy = (y - top) / scale;
    const std::string text = std::to_string(index);
    const int digit = gx / 6, col = gx % 6;
    if (gy < 7 && digit < int(text.size()) && col < 5)
      bright = digits[text[size_t(digit)] - '0'][gy * 5 + col] == '1';
  }
  const float v = bright ? 1.0f : 0.008f;
  return Sample{{v, v, v}, false, false, 0};
}

bool occluder(const SequenceConfig& c, double x, double y, int t) {
  if (c.scenario != SequenceScenario::MovingOccluder) return false;
  const double left = c.width * 0.12 + t * 1.8;
  const double top = c.height * 0.16 + t * 0.15;
  return x >= left && x < left + c.width * 0.24 && y >= top && y < top + c.height * 0.59;
}

// Procedural fixtures in scene-linear Rec.709 RGB. Fine structure is below the pixel Nyquist
// limit; these point-sampled fixtures are not a physical optics/anti-aliasing camera model.
Sample background(const SequenceConfig& c, double x, double y, bool alternate) {
  const double u = x / c.width, v = y / c.height;
  const double phase = alternate ? 1.9 : 0.0;
  Sample s{};
  const double margin = 2.5;
  if (v >= 0.76) {
    // Flat signal/exposure and colour patches from deep shadow through >1 scene-linear values.
    const int patch = std::max(0, std::min(5, int(std::floor(u * 6.0))));
    const double level[] = {0.003, 0.012, 0.055, 0.18, 0.9, 4.0};
    const double rgbScale[6][3] = {{1,1,1},{1.0,0.4,0.2},{0.3,0.7,1.0},{1,1,1},{1.0,0.15,0.35},{1,1,1}};
    for (int ch = 0; ch < 3; ++ch) s.rgb[ch] = float(level[patch] * rgbScale[patch][ch]);
    const double px = u * 6.0 - std::floor(u * 6.0);
    s.flat = y > c.height * 0.76 + margin && px * c.width / 6.0 > margin &&
             (1.0 - px) * c.width / 6.0 > margin;
  } else if (u < 0.33 && v < 0.50) {
    // Smooth skin-coloured tonal variation with weak fine pores (not a photograph/skin model).
    const double shape = std::exp(-9.0 * ((u - 0.16) * (u - 0.16) + (v - 0.25) * (v - 0.25)));
    const double pores = 0.004 * std::sin(2*kPi*(0.23*x + 0.03*y) + phase) * std::sin(2*kPi*0.19*y);
    const double light = 0.025 + 0.20 * shape + pores;
    s.rgb[0] = float(light); s.rgb[1] = float(light * 0.62); s.rgb[2] = float(light * 0.43);
    s.detail = x > margin && x < c.width*0.33-margin && y > margin && y < c.height*0.5-margin;
    s.detailRegion = 1;
  } else if (u < 0.67 && v < 0.50) {
    // Fine fabric weave; both low contrast fibres and narrow interwoven modulation matter.
    const double a = std::sin(2*kPi*(0.24*x + 0.035*y) + phase);
    const double b = std::sin(2*kPi*(0.025*x + 0.21*y) - phase);
    const double fibre = 0.18 + 0.035*a + 0.03*b + 0.012*a*b;
    s.rgb[0] = float(fibre * 0.72); s.rgb[1] = float(fibre * 0.92); s.rgb[2] = float(fibre);
    s.detail = x > c.width*0.33+margin && x < c.width*0.67-margin &&
               y > margin && y < c.height*0.5-margin;
    s.detailRegion = 2;
  } else if (v < 0.50) {
    // Aperiodically combined bands resembling fine foliage, without random ground-truth noise.
    const double leaves = 0.03*std::sin(2*kPi*(0.19*x+0.13*y) + phase) +
                          0.025*std::sin(2*kPi*(0.073*x-0.21*y) + 1.7) +
                          0.018*std::cos(2*kPi*(0.12*x+0.037*y) - phase);
    s.rgb[0] = float(0.055 + 0.3*leaves); s.rgb[1] = float(0.13 + leaves); s.rgb[2] = float(0.04 + 0.2*leaves);
    s.detail = x > c.width*0.67+margin && y > margin && y < c.height*0.5-margin;
    s.detailRegion = 3;
  } else if (u < 0.33) {
    // Thin hair-like lines angled over a dark, warm background. Frequency is sampled analytically.
    const double lines = std::pow(0.5+0.5*std::cos(2*kPi*(0.17*x+0.055*y) + phase), 2.0);
    s.rgb[0] = float(0.028 + 0.10*lines); s.rgb[1] = float(0.018 + 0.06*lines); s.rgb[2] = float(0.012 + 0.03*lines);
    s.detail = x > margin && x < c.width*0.33-margin &&
               y > c.height*0.5+margin && y < c.height*0.76-margin;
    s.detailRegion = 4;
  } else if (u < 0.67) {
    // A sharp slanted edge is a control, not included in the texture regression mask.
    const double side = x + 0.17*y > c.width*0.49 + c.height*0.11 ? 0.52 : 0.025;
    s.rgb[0] = float(side); s.rgb[1] = float(side*0.96); s.rgb[2] = float(side*0.85);
  } else {
    // Dark smooth gradients and saturated highlight help expose colour/brightness shifts.
    const double ramp = 0.006 + 0.30 * std::max(0.0, std::min(1.0, (u-0.67)/0.33));
    s.rgb[0] = float(ramp); s.rgb[1] = float(ramp*0.35); s.rgb[2] = float(ramp*0.8);
  }
  if (alternate) {
    const float r = s.rgb[0];
    s.rgb[0] = s.rgb[2] * 0.60f + 0.035f;
    s.rgb[2] = r * 1.3f + 0.008f;
  }
  return s;
}

Sample foreground(const SequenceConfig& c, double x, double y, int t) {
  const double localX = x - (c.width*0.12 + t*1.8);
  const double localY = y - (c.height*0.16 + t*0.15);
  const double fine = 0.015*std::sin(2*kPi*(0.22*localX + 0.09*localY));
  Sample s{{float(0.48+fine),float(0.10+0.4*fine),float(0.04+0.2*fine)},true,false,5};
  if (localX < 2.5 || localX > c.width*0.24-2.5 || localY < 2.5 || localY > c.height*0.59-2.5) s.detail = false;
  return s;
}

void validate(const SequenceConfig& c) {
  if (c.width < 8 || c.height < 8 || c.width > 16384 || c.height > 16384 ||
      uint64_t(c.width) * uint64_t(c.height) > 67108864ULL)
    throw std::invalid_argument("sequence dimensions must be >=8, <=16384 per axis and <=64M pixels");
  if (c.frameCount < 1 || c.frameCount > 1000000) throw std::invalid_argument("invalid sequence frame count");
  if (!std::isfinite(c.exposureStops) || c.exposureStops < -16.f || c.exposureStops > 16.f ||
      !std::isfinite(c.shotNoise) || c.shotNoise < 0.f || c.shotNoise > 1.f ||
      !std::isfinite(c.readNoise) || c.readNoise < 0.f || c.readNoise > 1.f ||
      !std::isfinite(c.panX) || !std::isfinite(c.panY) || std::fabs(c.panX)>1024.f || std::fabs(c.panY)>1024.f)
    throw std::invalid_argument("invalid sequence exposure/noise/motion parameters");
  switch (c.scenario) {
    case SequenceScenario::StaticDetail: case SequenceScenario::SubpixelPan:
    case SequenceScenario::MovingOccluder: case SequenceScenario::HardCut: case SequenceScenario::ExposureStep: break;
    default: throw std::invalid_argument("invalid sequence scenario");
  }
}
}  // namespace

std::vector<std::string> sequenceScenarioNames() { return {"static-detail", "subpixel-pan", "moving-occluder", "hard-cut", "exposure-step"}; }
const char* sequenceScenarioName(SequenceScenario s) {
  switch (s) {
    case SequenceScenario::StaticDetail: return "static-detail";
    case SequenceScenario::SubpixelPan: return "subpixel-pan";
    case SequenceScenario::MovingOccluder: return "moving-occluder";
    case SequenceScenario::HardCut: return "hard-cut";
    case SequenceScenario::ExposureStep: return "exposure-step";
  }
  throw std::invalid_argument("invalid sequence scenario");
}
SequenceScenario parseSequenceScenario(const std::string& s) {
  for (const auto v : {SequenceScenario::StaticDetail, SequenceScenario::SubpixelPan, SequenceScenario::MovingOccluder, SequenceScenario::HardCut, SequenceScenario::ExposureStep})
    if (s == sequenceScenarioName(v)) return v;
  throw std::invalid_argument("unknown sequence scenario: " + s);
}

SequenceFrame generateSequenceFrame(const SequenceConfig& c, int index) {
  validate(c);
  if (index < 0 || index >= c.frameCount) throw std::out_of_range("sequence frame index outside configured interval");
  const int w=c.width,h=c.height;
  SequenceFrame f;
  f.clean=vv::Image(w,h); f.noisy=vv::Image(w,h); f.linearClean=vv::Image(w,h);
  f.backwardFlow=vv::Plane(w,h,2);
  f.visibleMask=vv::Plane(w,h,1); f.disocclusionMask=vv::Plane(w,h,1);
  f.detailMask=vv::Plane(w,h,1); f.flatMask=vv::Plane(w,h,1);
  f.detailRegion=vv::Plane(w,h,1);
  f.frameIndex=index;
  const int change=c.frameCount/2;
  f.cut=c.scenario==SequenceScenario::HardCut && index==change && index>0;
  const bool alternate=c.scenario==SequenceScenario::HardCut && index>=change;
  const bool pan=c.scenario==SequenceScenario::SubpixelPan || c.scenario==SequenceScenario::MovingOccluder;
  const double dx=pan?c.panX:0.0,dy=pan?c.panY:0.0;
  const double gain=std::exp2(double(c.exposureStops)+(c.scenario==SequenceScenario::ExposureStep && index>=change?1.0:0.0));
  const uint32_t key=vv_hash3(c.seed,uint32_t(index),0x5371656eu);
  const int stripHeight=markerHeight(c);
  for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
    const double cx=x+0.5,cy=y+0.5;
    const bool fg=occluder(c,cx,cy,index);
    const bool marker=y<stripHeight;
    const Sample s=marker?frameMarker(c,x,y,index):(fg?foreground(c,cx,cy,index):background(c,cx+dx*index,cy+dy*index,alternate));
    for(int ch=0;ch<3;++ch) {
      const float linear=float(gain*s.rgb[ch]);
      const float var=c.shotNoise*std::max(linear,0.f)+c.readNoise*c.readNoise;
      const float noisy=linear+std::sqrt(var)*gauss(x,y,ch,key);
      f.linearClean.at(x,y)[ch]=linear;
      f.clean.at(x,y)[ch]=vv::acescctEncode(linear);
      f.noisy.at(x,y)[ch]=vv::acescctEncode(noisy);
    }
    f.clean.at(x,y)[3]=f.noisy.at(x,y)[3]=f.linearClean.at(x,y)[3]=1.f;
    f.detailMask.at(x,y)[0]=s.detail?1.f:0.f;
    f.detailRegion.at(x,y)[0]=s.detail?float(s.detailRegion):0.f;
    f.flatMask.at(x,y)[0]=s.flat?1.f:0.f;
    if(marker) continue; // all masks/regions/flow remain zero on diagnostic strip
    const double flowX=fg?-1.8:dx,flowY=fg?-0.15:dy;
    f.backwardFlow.at(x,y)[0]=float(flowX); f.backwardFlow.at(x,y)[1]=float(flowY);
    if(index==0 || f.cut) continue;
    const double px=x+flowX,py=y+flowY;
    const int x0=int(std::floor(px)),y0=int(std::floor(py)),x1=int(std::ceil(px)),y1=int(std::ceil(py));
    if(x0<0 || y0<stripHeight || x1>=w || y1>=h) continue;
    bool same=true;
    for(int yy:{y0,y1}) for(int xx:{x0,x1}) if(occluder(c,xx+0.5,yy+0.5,index-1)!=fg) same=false;
    f.visibleMask.at(x,y)[0]=same?1.f:0.f;
    f.disocclusionMask.at(x,y)[0]=!same && !fg?1.f:0.f;
  }
  return f;
}

}  // namespace eval
}  // namespace dn
