#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "dn_sequence.h"
#include "vv/log_transfer.h"

using namespace dn::eval;
namespace {
int checks=0,failures=0;
void check(bool b,const char* message) { ++checks; if(!b){++failures;std::fprintf(stderr,"FAIL %s\n",message);} }
void near(double a,double b,double tol,const char* message){check(std::fabs(a-b)<=tol,message);}
void rejects(const std::function<void()>& f,const char* message) { bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}check(thrown,message); }
void validMasks(const SequenceFrame& f) {
  for(const vv::Plane* p:{&f.visibleMask,&f.disocclusionMask,&f.detailMask,&f.flatMask}) {
    check(p->channels==1 && p->width==f.clean.width && p->height==f.clean.height,"mask layout");
    check(std::all_of(p->v.begin(),p->v.end(),[](float v){return v==0.f||v==1.f;}),"masks binary");
  }
  check(f.backwardFlow.channels==2,"flow has two components");
  check(f.detailRegion.channels==1 && f.detailRegion.width==f.clean.width && f.detailRegion.height==f.clean.height,"detail region layout");
  for(size_t i=0;i<f.detailRegion.v.size();++i) {
    const float label=f.detailRegion.v[i];
    check(label>=0.f && label<=5.f && label==std::floor(label),"region labels integer 0..5");
    check((label>0.f)==(f.detailMask.v[i]>0.5f),"region labels exactly cover detail mask");
  }
}
void testLog() {
  // Academy ACEScct specification reference points; curve only, not RGB-primary conversion.
  near(vv::acescctEncode(0.f),0.0729055341958355,1e-8,"log black pedestal");
  near(vv::acescctEncode(0.0078125f),0.155251141552511,2e-8,"log toe breakpoint");
  near(vv::acescctEncode(0.18f),0.4135884024924423,3e-8,"log middle grey");
  near(vv::acescctEncode(1.f),0.5547945205479452,3e-8,"log unit exposure");
  near(vv::acescctEncode(-0.01f),-0.0324968432207095,2e-8,"negative toe preserved");
  for(float v:{-1.f,-0.01f,0.f,0.001f,0.0078124f,0.0078125f,0.0078126f,0.18f,1.f,16.f,256.f})
    near(vv::acescctDecode(vv::acescctEncode(v)),v,2e-6*std::max(1.f,std::fabs(v)),"log inverse extended range");
  check(vv::acescctEncode(256.f)>vv::acescctEncode(1.f),"highlights not clipped");
  near(vv::acescctEncode(std::nextafter(0.0078125f,0.f)),vv::acescctEncode(std::nextafter(0.0078125f,1.f)),3e-8,"toe continuous");
}
void testDeterminism() {
  SequenceConfig c;c.width=96;c.height=64;
  const auto a=generateSequenceFrame(c,4);
  (void)generateSequenceFrame(c,8);(void)generateSequenceFrame(c,0);
  const auto b=generateSequenceFrame(c,4);
  check(a.clean.px==b.clean.px && a.noisy.px==b.noisy.px,"random access independent of history");
  const auto otherTime=generateSequenceFrame(c,3);
  check(a.clean.px==otherTime.clean.px,"static scene fixed");
  check(a.noisy.px!=otherTime.noisy.px,"noise changes per frame");
  ++c.seed;const auto otherSeed=generateSequenceFrame(c,4);
  check(a.clean.px==otherSeed.clean.px,"seed does not change clean reference");
  check(a.noisy.px!=otherSeed.noisy.px,"noise changes per seed");
  validMasks(a);
  check(std::count(a.detailMask.v.begin(),a.detailMask.v.end(),1.f)>100,"detail regions present");
  check(std::count(a.flatMask.v.begin(),a.flatMask.v.end(),1.f)>50,"flat calibration regions present");
  for(int region=1;region<=4;++region)
    check(std::count(a.detailRegion.v.begin(),a.detailRegion.v.end(),float(region))>10,"all static detail regions present");
  check(std::count(a.detailRegion.v.begin(),a.detailRegion.v.end(),5.f)==0,"foreground region absent in static scene");
  for(size_t i=3;i<a.clean.px.size();i+=4)check(a.clean.px[i]==1.f && a.noisy.px[i]==1.f,"alpha remains opaque");
}
void testZeroAndRange() {
  SequenceConfig c;c.width=80;c.height=48;c.shotNoise=c.readNoise=0;
  const auto f=generateSequenceFrame(c,3);
  check(f.clean.px==f.noisy.px,"zero noise exactly clean");
  for(int y=0;y<c.height;++y)for(int x=0;x<c.width;++x)for(int ch=0;ch<3;++ch)
    near(vv::acescctDecode(f.clean.at(x,y)[ch]),f.linearClean.at(x,y)[ch],2e-6,"encoded clean agrees with linear truth");
  c.exposureStops=-8;c.readNoise=0.02f;
  const auto noisy=generateSequenceFrame(c,1);
  check(std::all_of(noisy.noisy.px.begin(),noisy.noisy.px.end(),[](float v){return std::isfinite(v);}),"low light noisy image finite");
  int negative=0;
  for(size_t i=0;i<noisy.noisy.px.size();++i)if(i%4!=3 && vv::acescctDecode(noisy.noisy.px[i])<0)++negative;
  check(negative>100,"negative noisy linear samples are not clipped");
}
void testGeometry() {
  SequenceConfig c;c.width=96;c.height=64;c.scenario=SequenceScenario::SubpixelPan;c.shotNoise=c.readNoise=0;
  auto f=generateSequenceFrame(c,2);validMasks(f);
  near(f.backwardFlow.at(30,30)[0],c.panX,1e-7,"subpixel backward x flow");
  near(f.backwardFlow.at(30,30)[1],c.panY,1e-7,"subpixel backward y flow");
  check(f.visibleMask.at(30,30)[0]==1.f,"interior visible");
  check(f.visibleMask.at(c.width-1,10)[0]==0.f,"flow out of bounds excluded");
  c.panX=1.f;c.panY=0.f;
  const auto prev=generateSequenceFrame(c,1),cur=generateSequenceFrame(c,2);
  for(int y=0;y<c.height;++y)for(int x=0;x<c.width-1;++x)for(int ch=0;ch<3;++ch)
    near(cur.clean.at(x,y)[ch],prev.clean.at(x+1,y)[ch],1e-7,"integer motion agrees with analytic reference");
  const auto first=generateSequenceFrame(c,0);
  check(std::count(first.visibleMask.v.begin(),first.visibleMask.v.end(),1.f)==0,"first frame has no history");
  check(std::count(first.disocclusionMask.v.begin(),first.disocclusionMask.v.end(),1.f)==0,"first frame is not disocclusion");
}
void testOcclusion() {
  SequenceConfig c;c.width=96;c.height=64;c.scenario=SequenceScenario::MovingOccluder;
  const auto f=generateSequenceFrame(c,3);validMasks(f);
  check(std::count(f.disocclusionMask.v.begin(),f.disocclusionMask.v.end(),1.f)>10,"moving foreground exposes background");
  check(std::count(f.detailRegion.v.begin(),f.detailRegion.v.end(),5.f)>10,"foreground detail label present");
  int foregroundFlow=0,backgroundFlow=0;
  for(int y=0;y<c.height;++y)for(int x=0;x<c.width;++x) {
    const float dx=f.backwardFlow.at(x,y)[0];
    foregroundFlow+=std::fabs(dx+1.8f)<1e-6f;
    backgroundFlow+=std::fabs(dx-c.panX)<1e-6f;
    check(!(f.visibleMask.at(x,y)[0] && f.disocclusionMask.at(x,y)[0]),"disocclusions never marked visible");
  }
  check(foregroundFlow>100 && backgroundFlow>100,"independent foreground and camera motion");
}
void testCutsAndExposure() {
  SequenceConfig c;c.width=80;c.height=48;c.scenario=SequenceScenario::HardCut;c.shotNoise=c.readNoise=0;
  const int boundary=c.frameCount/2;
  const auto before=generateSequenceFrame(c,boundary-1),at=generateSequenceFrame(c,boundary),after=generateSequenceFrame(c,boundary+1);
  check(!before.cut && at.cut && !after.cut,"cut exactly once");
  check(before.clean.px!=at.clean.px,"cut changes scene");
  check(std::count(at.visibleMask.v.begin(),at.visibleMask.v.end(),1.f)==0,"cut forbids temporal history");
  check(std::count(after.visibleMask.v.begin(),after.visibleMask.v.end(),1.f)>0,"history restored after cut");
  c.scenario=SequenceScenario::ExposureStep;
  const auto low=generateSequenceFrame(c,boundary-1),high=generateSequenceFrame(c,boundary);
  check(!high.cut,"exposure change not a geometric cut");
  near(high.linearClean.at(10,10)[0],2.0*low.linearClean.at(10,10)[0],1e-7,"one stop exposure step");
  check(high.visibleMask.at(10,10)[0]==1.f,"exposure change retains geometric visibility");
}
void testMarker() {
  SequenceConfig c;c.width=128;c.height=72;c.shotNoise=c.readNoise=0;
  const auto normal=generateSequenceFrame(c,1);
  check(!c.frameMarker,"marker default disabled");
  const auto normalZero=generateSequenceFrame(c,0);
  check(normalZero.clean.px==normal.clean.px,"disabled static fixture contains no frame marker");
  c.frameMarker=true;
  const auto first=generateSequenceFrame(c,0),second=generateSequenceFrame(c,1);
  validMasks(second);
  check(first.clean.px!=second.clean.px,"decimal marker distinguishes static frames");
  near(first.linearClean.at(3,2)[0],0.25,1e-7,"zero glyph bright top stroke");
  near(first.linearClean.at(2,2)[0],0.002,1e-7,"zero glyph dark top corner");
  near(second.linearClean.at(4,2)[0],0.25,1e-7,"one glyph bright top centre");
  near(second.linearClean.at(3,2)[0],0.002,1e-7,"one glyph dark top neighbour");
  for(int y=0;y<11;++y)for(int x=0;x<c.width;++x) {
    check(second.visibleMask.at(x,y)[0]==0 && second.detailMask.at(x,y)[0]==0 &&
          second.detailRegion.at(x,y)[0]==0 && second.flatMask.at(x,y)[0]==0 &&
          second.disocclusionMask.at(x,y)[0]==0,"marker excluded from every mask");
    check(second.backwardFlow.at(x,y)[0]==0 && second.backwardFlow.at(x,y)[1]==0,"marker flow invalidated");
  }
  for(int y=11;y<c.height;++y)for(int x=0;x<c.width;++x)for(int ch=0;ch<4;++ch)
    check(second.clean.at(x,y)[ch]==normal.clean.at(x,y)[ch],"marker does not change pixels outside strip");
  c.scenario=SequenceScenario::SubpixelPan;c.panY=-0.25f;
  const auto pan=generateSequenceFrame(c,2);
  check(pan.visibleMask.at(40,11)[0]==0,"previous-frame footprint touching marker is excluded");
  c.shotNoise=0.0001f;c.readNoise=0.0005f;
  const auto noisy=generateSequenceFrame(c,2);
  check(noisy.clean.px!=noisy.noisy.px,"marker fixtures use normal noise path");
}
void testValidation() {
  SequenceConfig c;c.width=48;c.height=32;
  for(const auto& name:sequenceScenarioNames())check(name==sequenceScenarioName(parseSequenceScenario(name)),"scenario names round trip");
  rejects([&]{parseSequenceScenario("made-up");},"unknown scenario rejected");
  rejects([&]{generateSequenceFrame(c,-1);},"negative index rejected");
  rejects([&]{generateSequenceFrame(c,c.frameCount);},"out of interval index rejected");
  c.width=-2;rejects([&]{generateSequenceFrame(c,0);},"negative dimensions rejected before allocation");
  c.width=48;c.frameCount=0;rejects([&]{generateSequenceFrame(c,0);},"empty interval rejected");
  c.frameCount=12;c.readNoise=-0.1f;rejects([&]{generateSequenceFrame(c,0);},"negative noise rejected");
  c.readNoise=0;c.panX=std::numeric_limits<float>::quiet_NaN();rejects([&]{generateSequenceFrame(c,0);},"NaN motion rejected");
  c.panX=0;c.scenario=static_cast<SequenceScenario>(999);rejects([&]{generateSequenceFrame(c,0);},"invalid enum rejected");
}
void testNoiseVariance() {
  SequenceConfig c;c.width=320;c.height=180;c.exposureStops=0.f;c.scenario=SequenceScenario::StaticDetail;
  c.shotNoise=0.0001f;c.readNoise=0.0005f;
  double mean=0,second=0;int count=0;
  for(int frame=0;frame<5;++frame) {
    const auto f=generateSequenceFrame(c,frame);
    // First flat patch is neutral linear 0.003, independent samples from all channels/frames.
    for(int y=145;y<175;++y)for(int x=8;x<40;++x)for(int ch=0;ch<3;++ch) {
      const double d=vv::acescctDecode(f.noisy.at(x,y)[ch])-f.linearClean.at(x,y)[ch];
      mean+=d;second+=d*d;++count;
    }
  }
  mean/=count;second=second/count-mean*mean;
  const double expected=c.shotNoise*0.003+c.readNoise*c.readNoise;
  check(std::fabs(mean)<0.04*std::sqrt(expected),"linear noise mean near zero");
  check(second/expected>0.94 && second/expected<1.06,"linear flat noise matches declared variance");
}
}
int main() {
  for(const auto& test:std::vector<std::pair<const char*,std::function<void()>>>{{"log curve",testLog},{"determinism",testDeterminism},{"range",testZeroAndRange},{"flow",testGeometry},{"occlusion",testOcclusion},{"cuts and exposure",testCutsAndExposure},{"frame marker",testMarker},{"validation",testValidation},{"noise variance",testNoiseVariance}}) {
    const int before=failures;test.second();std::printf("%s %s\n",before==failures?"ok":"FAIL",test.first);
  }
  std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
