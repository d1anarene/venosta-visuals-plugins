// Research benchmark and float interchange; deliberately contains no production denoiser.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "dn_sequence.h"
#include "dn_sequence_metrics.h"
#include "io/pfm.h"
#include "vv/blur.h"

#ifndef DN_SOURCE_REVISION
#define DN_SOURCE_REVISION "unknown"
#endif
#ifndef DN_SOURCE_DIGEST
#define DN_SOURCE_DIGEST "unknown"
#endif
namespace fs=std::filesystem;
using namespace dn::eval;
namespace {
struct Options {
  SequenceConfig config;
  std::string mode="benchmark",scenario="all",output,dataset,candidate,candidateName,candidateSettings,datasetRevision,datasetDigest;
  std::vector<uint32_t> seeds={7,19,43};
  int fpsNum=25,fpsDen=1;
};
int integer(const std::string& s,int lo,int hi) {
  size_t n=0; long long v=std::stoll(s,&n);
  if(n!=s.size() || v<lo || v>hi) throw std::invalid_argument("integer outside allowed range: "+s);
  return int(v);
}
float real(const std::string& s) {
  size_t n=0; float v=std::stof(s,&n);
  if(n!=s.size() || !std::isfinite(v)) throw std::invalid_argument("expected finite number: "+s);
  return v;
}
Options options(int argc,char** argv) {
  Options o; o.config.width=160; o.config.height=90; o.config.frameCount=8;
  if(argc>1) o.mode=argv[1];
  for(int a=2;a<argc;a+=2) {
    if(a+1>=argc) throw std::invalid_argument("option needs a value");
    const std::string k=argv[a],v=argv[a+1];
    if(o.mode=="score" && k!="--output" && k!="--dataset" && k!="--candidate-dir" &&
       k!="--candidate-name" && k!="--candidate-settings")
      throw std::invalid_argument("score uses dataset configuration, not sequence options: "+k);
    if(k=="--width") o.config.width=integer(v,32,8192);
    else if(k=="--height") o.config.height=integer(v,32,8192);
    else if(k=="--frames") o.config.frameCount=integer(v,2,512);
    else if(k=="--exposure") o.config.exposureStops=real(v);
    else if(k=="--shot-noise") o.config.shotNoise=real(v);
    else if(k=="--read-noise") o.config.readNoise=real(v);
    else if(k=="--pan-x") o.config.panX=real(v);
    else if(k=="--pan-y") o.config.panY=real(v);
    else if(k=="--frame-marker") o.config.frameMarker=integer(v,0,1)!=0;
    else if(k=="--scenario") o.scenario=v;
    else if(k=="--output") o.output=v;
    else if(k=="--dataset") o.dataset=v;
    else if(k=="--candidate-dir") o.candidate=v;
    else if(k=="--candidate-name") o.candidateName=v;
    else if(k=="--candidate-settings") o.candidateSettings=v;
    else if(k=="--fps-num") o.fpsNum=integer(v,1,240000);
    else if(k=="--fps-den") o.fpsDen=integer(v,1,10000);
    else if(k=="--seeds") {
      o.seeds.clear(); std::istringstream s(v); std::string seed;
      while(std::getline(s,seed,',')) o.seeds.push_back(uint32_t(integer(seed,0,2147483647)));
      if(o.seeds.empty() || o.seeds.size()>32 || v.back()==',') throw std::invalid_argument("invalid seed list");
      auto sorted=o.seeds; std::sort(sorted.begin(),sorted.end());
      if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end()) throw std::invalid_argument("duplicate seeds");
    } else throw std::invalid_argument("unknown option: "+k);
  }
  if(o.mode!="benchmark" && o.mode!="export" && o.mode!="score") throw std::invalid_argument("mode must be benchmark, export or score");
  if(uint64_t(o.config.width)*o.config.height>33554432u) throw std::invalid_argument("maximum frame size is 32M pixels");
  if(o.scenario!="all") o.config.scenario=parseSequenceScenario(o.scenario);
  return o;
}
std::string quote(const std::string& s) {
  std::ostringstream o; o<<'"';
  for(unsigned char c:s) {
    if(c=='"'||c=='\\') o<<'\\'<<c;
    else if(c<32) o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
    else o<<c;
  }
  return o.str()+'"';
}
std::string num(double x) {
  if(!std::isfinite(x)) return "null";
  std::ostringstream o; o<<std::setprecision(12)<<x; return o.str();
}
std::string metric(const Metric& m) { return "{\"value\":"+num(m.value)+",\"pixels\":"+std::to_string(m.samples)+"}"; }
std::string regionsJson(const vv::Image& output,const vv::Image& clean,const vv::Plane& regions) {
  const std::vector<std::string> names={"skin","fabric","foliage","hair","foreground"};
  if(regions.channels!=1 || regions.width!=clean.width || regions.height!=clean.height ||
     regions.v.size()!=size_t(clean.width)*clean.height) throw std::runtime_error("invalid detail regions");
  for(float value:regions.v) if(!std::isfinite(value)||value<0||value>5||value!=std::floor(value))
    throw std::runtime_error("invalid detail-region label");
  std::ostringstream s; s<<'{';
  vv::Plane empty(clean.width,clean.height,1);
  for(int id=1;id<=5;++id) {
    vv::Plane mask=empty;
    for(size_t p=0;p<mask.v.size();++p) mask.v[p]=regions.v[p]==id?1.f:0.f;
    const auto detail=frameQuality(output,clean,mask,empty,empty);
    const auto error=maskedRgbError(output,clean,mask);
    if(id>1) s<<',';
    s<<quote(names[id-1])<<":{\"detail_transfer\":"<<metric(detail.detailTransfer)
     <<",\"rgb_rmse\":"<<metric(error.rmse)<<",\"rgb_bias\":["<<num(error.bias[0])<<','<<num(error.bias[1])<<','<<num(error.bias[2])<<"]}";
  }
  return s.str()+'}';
}
std::string hex(uint64_t n) { std::ostringstream s; s<<std::hex<<n; return s.str(); }
uint64_t fileFingerprint(const fs::path& p) {
  std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("cannot fingerprint "+p.string());
  uint64_t h=14695981039346656037ull; char c;
  while(f.get(c)) { h^=static_cast<unsigned char>(c); h*=1099511628211ull; }
  if(!f.eof()) throw std::runtime_error("fingerprint read failed: "+p.string());
  return h;
}
std::string filename(const std::string& prefix,int t) {
  std::ostringstream s; s<<prefix<<'-'<<std::setfill('0')<<std::setw(4)<<t<<".pfm"; return s.str();
}
void checkedFile(const fs::path& path,const std::string& data) {
  // Binary mode: identical bytes (and fingerprints) on every OS; no CRLF translation on Windows.
  std::ofstream f(path,std::ios::binary); if(!f) throw std::runtime_error("cannot write "+path.string());
  f<<data; if(!f) throw std::runtime_error("write failed: "+path.string());
}
vv::Image planeImage(const vv::Plane& p) {
  vv::Image i(p.width,p.height);
  for(int y=0;y<p.height;++y) for(int x=0;x<p.width;++x) {
    for(int c=0;c<3;++c) i.at(x,y)[c]=p.at(x,y)[p.channels==1?0:std::min(c,p.channels-1)];
    i.at(x,y)[3]=1;
  }
  return i;
}
vv::Plane imagePlane(const vv::Image& i,int channels) {
  vv::Plane p(i.width,i.height,channels);
  for(int y=0;y<i.height;++y) for(int x=0;x<i.width;++x) for(int c=0;c<channels;++c) p.at(x,y)[c]=i.at(x,y)[c];
  return p;
}
std::string configText(const Options& o) {
  const auto& c=o.config; std::ostringstream s; s<<std::setprecision(9);
  s<<"schema dn-sequence-dataset-v1\ngenerator "<<kSequenceGeneratorVersion<<"\nsource_revision "<<DN_SOURCE_REVISION
   <<"\nsource_digest "<<DN_SOURCE_DIGEST<<"\nscenario "<<o.scenario
   <<"\nwidth "<<c.width<<"\nheight "<<c.height<<"\nframes "<<c.frameCount<<"\nseed "<<c.seed
   <<"\nexposure "<<c.exposureStops<<"\nshot "<<c.shotNoise<<"\nread "<<c.readNoise
   <<"\npan_x "<<c.panX<<"\npan_y "<<c.panY<<"\nframe_marker "<<int(c.frameMarker)
   <<"\nfps_num "<<o.fpsNum<<"\nfps_den "<<o.fpsDen<<'\n';
  return s.str();
}
Options readConfig(const fs::path& dir) {
  std::ifstream f(dir/"dataset.conf"); if(!f) throw std::runtime_error("missing dataset.conf");
  std::map<std::string,std::string> m; std::string line;
  while(std::getline(f,line)) {
    std::istringstream s(line); std::string k,v,e;
    if(!(s>>k>>v)||s>>e||!m.emplace(k,v).second) throw std::runtime_error("malformed dataset.conf");
  }
  if(m.size()!=17 || m.at("schema")!="dn-sequence-dataset-v1" || m.at("generator")!=kSequenceGeneratorVersion)
    throw std::runtime_error("unsupported dataset schema/generator");
  Options o; o.scenario=m.at("scenario"); o.config.scenario=parseSequenceScenario(o.scenario);
  o.datasetRevision=m.at("source_revision");
  o.datasetDigest=m.at("source_digest");
  o.config.width=integer(m.at("width"),32,8192); o.config.height=integer(m.at("height"),32,8192);
  vv::io::validatePfmSize(o.config.width,o.config.height);
  o.config.frameCount=integer(m.at("frames"),2,512); o.config.seed=uint32_t(integer(m.at("seed"),0,2147483647));
  o.config.exposureStops=real(m.at("exposure")); o.config.shotNoise=real(m.at("shot")); o.config.readNoise=real(m.at("read"));
  o.config.panX=real(m.at("pan_x")); o.config.panY=real(m.at("pan_y"));
  o.config.frameMarker=integer(m.at("frame_marker"),0,1)!=0;
  o.fpsNum=integer(m.at("fps_num"),1,240000); o.fpsDen=integer(m.at("fps_den"),1,10000);
  return o;
}
std::string metadata(const Options& o) {
  const auto& c=o.config;
  return "\"schema\":\"dn-sequence-report-v1\",\"generator\":"+quote(kSequenceGeneratorVersion)+
    ",\"tool_source_revision\":"+quote(DN_SOURCE_REVISION)+",\"dataset_source_revision\":"+
    (o.datasetRevision.empty()?"null":quote(o.datasetRevision))+",\"encoding\":"+quote(kSequenceEncoding)+
    ",\"tool_source_sha256\":"+quote(DN_SOURCE_DIGEST)+",\"dataset_source_sha256\":"+
    (o.datasetDigest.empty()?"null":quote(o.datasetDigest))+
    ",\"noise_model\":"+quote(kSequenceNoiseModel)+",\"width\":"+std::to_string(c.width)+
    ",\"height\":"+std::to_string(c.height)+",\"frames\":"+std::to_string(c.frameCount)+
    ",\"fps_num\":"+std::to_string(o.fpsNum)+",\"fps_den\":"+std::to_string(o.fpsDen)+
    ",\"exposure_stops\":"+num(c.exposureStops)+",\"shot_variance_per_linear_unit\":"+num(c.shotNoise)+
    ",\"read_linear_sigma\":"+num(c.readNoise)+",\"pan_pixels_per_frame\":["+num(c.panX)+","+num(c.panY)+"]"+
    ",\"frame_marker\":"+(c.frameMarker?"true":"false")+
    ",\"metric_domain\":\"encoded RGB, equal channel weights; no display transform; undefined values are null\""+
    ",\"limitations\":\"procedural scenes, independent Gaussian noise, oracle geometry, no calibrated camera/ISP; controls are not denoisers\"";
}
std::string qualityJson(const std::string& scenario,uint32_t seed,int t,const std::string& method,
                        const FrameQuality& q,const Metric& temporal,bool cut,double elapsed,
                        const vv::Image& output,const vv::Image& clean,const vv::Plane& regions) {
  std::ostringstream s;
  s<<"{\"scenario\":"<<quote(scenario)<<",\"seed\":"<<seed<<",\"frame\":"<<t
   <<",\"method\":"<<quote(method)<<",\"cut\":"<<(cut?"true":"false")
   <<",\"rgb_rmse\":"<<metric(q.all.rmse)<<",\"rgb_bias\":["<<num(q.all.bias[0])<<','<<num(q.all.bias[1])<<','<<num(q.all.bias[2])<<']'
   <<",\"detail_transfer\":"<<metric(q.detailTransfer)<<",\"removed_structure\":"<<metric(q.removedStructure)
   <<",\"flat_residual_sigma\":"<<metric(q.flatResidualSigma)<<",\"disocclusion_rmse\":"<<metric(q.disocclusionRmse)
   <<",\"temporal_residual_rmse\":"<<metric(temporal)<<",\"control_compute_ms\":"<<num(elapsed)
   <<",\"output_rgb_fnv1a64\":"<<quote(hex(vv::io::rgbFingerprint(output)))
   <<",\"clean_rgb_fnv1a64\":"<<quote(hex(vv::io::rgbFingerprint(clean)))
   <<",\"detail_regions\":"<<regionsJson(output,clean,regions)<<'}';
  return s.str();
}
vv::Image control(const std::string& name,const SequenceFrame& current,const SequenceFrame* previous,const vv::Image& first) {
  if(name=="clean-oracle") return current.clean;
  if(name=="noisy") return current.noisy;
  if(name=="frozen-clean") return first;
  if(name=="blur-0.8") {
    vv::Plane p(current.noisy.width,current.noisy.height,4); p.v=current.noisy.px;
    auto b=vv::gaussianBlurDirect(p,0.8f); vv::Image out=current.noisy; out.px=std::move(b.v); return out;
  }
  vv::Image out=current.noisy;
  if(previous) for(size_t i=0;i<out.px.size();++i) if(i%4!=3) out.px[i]=0.5f*(out.px[i]+previous->noisy.px[i]);
  return out; // deliberately unaligned, cut-unaware trailing control, not a recommended algorithm
}
void benchmark(const Options& o) {
  const auto names=o.scenario=="all"?sequenceScenarioNames():std::vector<std::string>{o.scenario};
  std::ostringstream report; report<<'{'<<metadata(o)<<",\"rows\":[\n"; bool comma=false;
  for(const auto& name:names) for(uint32_t seed:o.seeds) {
    auto c=o.config; c.seed=seed; c.scenario=parseSequenceScenario(name);
    SequenceFrame previous; std::map<std::string,vv::Image> previousOutputs; vv::Image first;
    for(int t=0;t<c.frameCount;++t) {
      auto frame=generateSequenceFrame(c,t); if(t==0) first=frame.clean;
      for(const auto& method:std::vector<std::string>{"clean-oracle","noisy","blur-0.8","frozen-clean","unaligned-temporal"}) {
        const auto begin=std::chrono::steady_clock::now();
        auto out=control(method,frame,t?&previous:nullptr,first);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const auto q=frameQuality(out,frame.clean,frame.detailMask,frame.flatMask,frame.disocclusionMask);
        Metric temporal;
        if(t) temporal=temporalResidualError(out,frame.clean,previousOutputs.at(method),previous.clean,frame.backwardFlow,frame.visibleMask);
        if(comma) report<<",\n"; comma=true;
        report<<qualityJson(name,seed,t,method,q,temporal,frame.cut,ms,out,frame.clean,frame.detailRegion);
        previousOutputs[method]=std::move(out);
      }
      previous=std::move(frame);
    }
  }
  report<<"\n]}\n";
  if(o.output.empty()) std::cout<<report.str(); else checkedFile(o.output,report.str());
}
void exportDataset(Options o) {
  if(o.output.empty() || o.scenario=="all" || o.seeds.size()!=1)
    throw std::invalid_argument("export needs --output NEW_DIRECTORY --scenario NAME --seeds ONE_SEED");
  const fs::path dir=o.output;
  if(fs::exists(dir)) throw std::runtime_error("export directory already exists; use a new directory");
  o.config.seed=o.seeds.front(); o.datasetRevision=DN_SOURCE_REVISION; o.datasetDigest=DN_SOURCE_DIGEST;
  (void)generateSequenceFrame(o.config,0); // Validate before creating an output directory.
  fs::create_directories(dir);
  checkedFile(dir/"dataset.conf",configText(o));
  std::ostringstream fingerprints,frames;
  for(int t=0;t<o.config.frameCount;++t) {
    const auto f=generateSequenceFrame(o.config,t);
    const std::vector<std::pair<std::string,vv::Image>> images={
      {"clean",f.clean},{"noisy",f.noisy},{"flow",planeImage(f.backwardFlow)},
      {"visible",planeImage(f.visibleMask)},{"disocclusion",planeImage(f.disocclusionMask)},
      {"detail",planeImage(f.detailMask)},{"flat",planeImage(f.flatMask)},
      {"detail-regions",planeImage(f.detailRegion)}};
    for(const auto& item:images) {
      const auto file=filename(item.first,t); vv::io::writePfm((dir/file).string(),item.second);
      fingerprints<<file<<' '<<std::hex<<vv::io::rgbFingerprint(item.second)<<std::dec<<'\n';
    }
    if(t) frames<<',';
    frames<<"{\"frame\":"<<t<<",\"cut\":"<<(f.cut?"true":"false")<<'}';
  }
  checkedFile(dir/"manifest.json","{"+metadata(o)+",\"scenario\":"+quote(o.scenario)+",\"seed\":"+std::to_string(o.config.seed)+
    ",\"storage\":\"PFM RGB float32, bottom-up file rows, unit scale; alpha omitted/opaque; no transform\",\"fingerprints\":\"FNV1a64 of RGB float bits (not cryptographic)\",\"frame_metadata\":["+frames.str()+"]}\n");
  for(const auto& name:{"dataset.conf","manifest.json"}) fingerprints<<name<<' '<<std::hex<<fileFingerprint(dir/name)<<std::dec<<'\n';
  checkedFile(dir/"fingerprints.txt",fingerprints.str());
  std::cout<<"Exported "<<o.config.frameCount<<" frames and references to "<<dir.string()<<'\n';
}
void score(const Options& requested) {
  if(requested.dataset.empty()||requested.candidate.empty()||requested.candidateName.empty())
    throw std::invalid_argument("score needs --dataset DIR --candidate-dir DIR --candidate-name NAME");
  const fs::path dir=requested.dataset;
  auto o=readConfig(dir); o.output=requested.output;
  std::ifstream list(dir/"fingerprints.txt"); if(!list) throw std::runtime_error("missing dataset fingerprints");
  std::map<std::string,uint64_t> hashes; std::string line;
  while(std::getline(list,line)) {
    std::istringstream row(line); std::string name,extra; uint64_t hash=0;
    if(!(row>>name>>std::hex>>hash)||(row>>extra)||!hashes.emplace(name,hash).second) throw std::runtime_error("bad fingerprint list");
  }
  if(hashes.size()!=size_t(o.config.frameCount)*8+2) throw std::runtime_error("incomplete dataset fingerprints");
  for(const auto& name:{"dataset.conf","manifest.json"})
    if(hashes.at(name)!=fileFingerprint(dir/name)) throw std::runtime_error("dataset metadata mismatch");
  auto load=[&](const std::string& prefix,int t) {
    const auto name=filename(prefix,t); auto image=vv::io::readPfm((dir/name).string());
    if(image.width!=o.config.width||image.height!=o.config.height || vv::io::rgbFingerprint(image)!=hashes.at(name))
      throw std::runtime_error("dataset mismatch: "+name);
    return image;
  };
  std::ostringstream report; report<<'{'<<metadata(o)<<",\"scenario\":"<<quote(o.scenario)<<",\"seed\":"<<o.config.seed
    <<",\"candidate_name\":"<<quote(requested.candidateName)<<",\"candidate_settings\":"<<quote(requested.candidateSettings)<<",\"rows\":[\n";
  vv::Image previousOut,previousClean;
  for(int t=0;t<o.config.frameCount;++t) {
    auto clean=load("clean",t); (void)load("noisy",t);
    const auto flow=imagePlane(load("flow",t),2),visible=imagePlane(load("visible",t),1);
    const auto detail=imagePlane(load("detail",t),1),flat=imagePlane(load("flat",t),1),dis=imagePlane(load("disocclusion",t),1);
    const auto regions=imagePlane(load("detail-regions",t),1);
    auto out=vv::io::readPfm((fs::path(requested.candidate)/filename("output",t)).string());
    const auto q=frameQuality(out,clean,detail,flat,dis);
    Metric temporal; if(t) temporal=temporalResidualError(out,clean,previousOut,previousClean,flow,visible);
    const bool cut=o.config.scenario==SequenceScenario::HardCut && t==o.config.frameCount/2;
    if(t) report<<",\n";
    report<<qualityJson(o.scenario,o.config.seed,t,requested.candidateName,q,temporal,cut,std::numeric_limits<double>::quiet_NaN(),out,clean,regions);
    previousOut=std::move(out); previousClean=std::move(clean);
  }
  report<<"\n]}\n"; if(o.output.empty()) std::cout<<report.str(); else checkedFile(o.output,report.str());
}
} // namespace
int main(int argc,char** argv) {
  try {
    auto o=options(argc,argv);
    if(o.mode=="benchmark") benchmark(o); else if(o.mode=="export") exportDataset(o); else score(o);
    return 0;
  } catch(const std::exception& e) {
    std::cerr<<"dn_sequence_tool: "<<e.what()<<"\n"
      "benchmark [--scenario all|NAME --seeds 7,19,43 --frames 8 --width 160 --height 90 --output report.json]\n"
      "export --scenario NAME --seeds ONE --output NEW_DIRECTORY [same sequence options]\n"
      "score --dataset EXPORTED_DIRECTORY --candidate-dir OUTPUT_DIRECTORY --candidate-name NAME [--candidate-settings TEXT --output report.json]\n";
    return 2;
  }
}
