// Opt-in research plugin. Temporal checkouts are observed, never assumed to match params[0].
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHelper.h"
#include "Param_Utils.h"
#include "PrSDKAESupport.h"
#include "entry.h"
#include "vv/pixel_io.h"
#include "probe_common.h"
#include <limits>

namespace {
#ifdef VV_PROBE_GPU
constexpr const char* kName="Temporal Probe Metal (research)";
constexpr const char* kMatch="VVIS Research TemporalProbeMetal";
#else
constexpr const char* kName="Temporal Probe CPU (research)";
constexpr const char* kMatch="VVIS Research TemporalProbeCPU";
#endif
enum { Input, Offset, View, Gain, Logging, Analyse, MeasuredMean, ParamCount };
vv::PixelLayout layout(PF_InData* in_data, PF_OutData* out_data, PF_EffectWorld* world) {
  if (in_data->appl_id=='PrMr') {
    AEFX_SuiteScoper<PF_PixelFormatSuite1> suite(in_data,kPFPixelFormatSuite,kPFPixelFormatSuiteVersion1,out_data);
    PrPixelFormat f=PrPixelFormat_Invalid;
    if (suite->GetPixelFormat(world,&f)) throw std::runtime_error("pixel format unavailable");
    if(f==PrPixelFormat_BGRA_4444_32f) return vv::PixelLayout::BGRA_32f;
    if(f==PrPixelFormat_BGRA_4444_8u) return vv::PixelLayout::BGRA_8u;
    throw std::runtime_error("unsupported Premiere checkout format");
  }
  AEFX_SuiteScoper<PF_WorldSuite2> suite(in_data,kPFWorldSuite,kPFWorldSuiteVersion2,out_data);
  PF_PixelFormat f=PF_PixelFormat_ARGB32; suite->PF_GetPixelFormat(world,&f);
  return f==PF_PixelFormat_ARGB128 ? vv::PixelLayout::ARGB_32f : f==PF_PixelFormat_ARGB64 ? vv::PixelLayout::ARGB_16u : vv::PixelLayout::ARGB_8u;
}
vv::Image read(PF_InData* in_data, PF_OutData* out_data, PF_EffectWorld* world) {
  vv::Image img;
  vv::readPixels(world->data,world->rowbytes,world->width,world->height,layout(in_data,out_data,world),img);
  return img;
}
struct Checkout {
  PF_InData* data; PF_ParamDef value{}; bool valid=false;
  explicit Checkout(PF_InData* d):data(d){}
  PF_Err get(A_long time) {
    PF_Err e=PF_CHECKOUT_PARAM(data,0,time,data->time_step,data->time_scale,&value);
    valid=e==PF_Err_NONE; return e;
  }
  ~Checkout(){ if(valid) PF_CHECKIN_PARAM(data,&value); }
};
PF_Err setup(PF_InData* in_data,PF_OutData* out_data) {
  out_data->my_version=PF_VERSION(1,0,0,PF_Stage_DEVELOP,1);
  out_data->out_flags=PF_OutFlag_DEEP_COLOR_AWARE|PF_OutFlag_WIDE_TIME_INPUT;
  out_data->out_flags2=PF_OutFlag2_FLOAT_COLOR_AWARE|PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
  if(in_data->appl_id=='PrMr'){
    AEFX_SuiteScoper<PF_PixelFormatSuite1> suite(in_data,kPFPixelFormatSuite,kPFPixelFormatSuiteVersion1,out_data);
    suite->ClearSupportedPixelFormats(in_data->effect_ref);
    suite->AddSupportedPixelFormat(in_data->effect_ref,PrPixelFormat_BGRA_4444_32f);
    suite->AddSupportedPixelFormat(in_data->effect_ref,PrPixelFormat_BGRA_4444_8u);
  }
  return PF_Err_NONE;
}
PF_Err parameters(PF_InData* in_data,PF_OutData* out_data) {
  PF_ParamDef def;
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("Neighbour offset (frames)",-3,3,-3,3,-1,1);
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("View: 0 current / 1 neighbour / 2 difference",0,2,0,2,0,2);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Difference gain",0,1000,0,10,1,PF_Precision_HUNDREDTHS,PF_ValueDisplayFlag_NONE,0,3);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Log diagnostics",false,PF_ParamFlag_CANNOT_TIME_VARY,4);
  AEFX_CLR_STRUCT(def); PF_ADD_BUTTON("Measure current input","Measure",0,PF_ParamFlag_SUPERVISE,5);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Saved measured RGB mean",-1e6,1e6,-1,1,0,PF_Precision_HUNDREDTHS,PF_ValueDisplayFlag_NONE,PF_ParamFlag_CANNOT_TIME_VARY,6);
  out_data->num_params=ParamCount; return PF_Err_NONE;
}
PF_Err user(PF_InData* in_data,PF_OutData* out_data,PF_ParamDef* p[],void* extra) {
  if(static_cast<PF_UserChangedParamExtra*>(extra)->param_index!=Analyse) return PF_Err_NONE;
  Checkout checkout(in_data);
  PF_Err err=checkout.get(in_data->current_time);
  if(err || !checkout.value.u.ld.data){
    // Record why the action could not measure (Premiere may not hand out pixels during a UI action).
    if(p[Logging]->u.bd.value){
      std::ostringstream s;
      s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"premiere-cpu\",\"action\":\"user-analysis-failed\",\"time_ticks\":"
        << in_data->current_time << ",\"checkout_error\":" << err << ",\"data_present\":" << (checkout.value.u.ld.data ? "true" : "false") << '}';
      vv::probe::log(s.str());
    }
    return err ? err : PF_Err_INVALID_CALLBACK;
  }
  const vv::Image image=read(in_data,out_data,&checkout.value.u.ld);
  const double measured=vv::probe::mean(image);
  p[MeasuredMean]->u.fs_d.value=measured;
  p[MeasuredMean]->uu.change_flags=PF_ChangeFlag_CHANGED_VALUE;
  out_data->out_flags|=PF_OutFlag_REFRESH_UI;
  if(p[Logging]->u.bd.value){
    std::ostringstream s; s.precision(17);
    s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"premiere-cpu\",\"action\":\"user-analysis\",\"time_ticks\":"
      << in_data->current_time << ",\"time_scale\":" << in_data->time_scale << ",\"measured_mean\":" << measured
      << ',' << vv::probe::imageFields(image,nullptr) << '}'; vv::probe::log(s.str());
  }
  return PF_Err_NONE;
}
PF_Err render(PF_InData* in_data,PF_OutData* out_data,PF_ParamDef* p[],PF_LayerDef* output) {
  PF_EffectWorld* input=&p[0]->u.ld;
  if(!input->data || !output || !output->data || input->width!=output->width || input->height!=output->height ||
     layout(in_data,out_data,input)!=layout(in_data,out_data,output)) return PF_Err_INVALID_CALLBACK;
  const int offset=p[Offset]->u.sd.value, mode=p[View]->u.sd.value;
  if(offset < -3 || offset > 3 || mode < 0 || mode > 2 || in_data->time_step <= 0 || in_data->time_scale <= 0)
    return PF_Err_INVALID_CALLBACK;
  const int64_t requested=int64_t(in_data->current_time)+int64_t(offset)*in_data->time_step;
  if(requested<std::numeric_limits<A_long>::min() || requested>std::numeric_limits<A_long>::max()) return PF_Err_INVALID_CALLBACK;
  Checkout checkout(in_data); PF_Err err=checkout.get(static_cast<A_long>(requested));
  const vv::Image current=read(in_data,out_data,input);
  vv::Image neighbour; const vv::Image* other=nullptr;
  if(!err && checkout.value.u.ld.data){ neighbour=read(in_data,out_data,&checkout.value.u.ld); other=&neighbour; }
  if(p[Logging]->u.bd.value){
    std::ostringstream s; s.precision(17);
    s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"premiere-cpu\",\"action\":\"render\",\"time_ticks\":" << in_data->current_time
      << ",\"requested_neighbour_ticks\":" << requested << ",\"time_step\":" << in_data->time_step << ",\"time_scale\":" << in_data->time_scale
      << ",\"returned_time_verified\":false,\"offset_frames\":" << offset << ",\"view\":" << mode << ",\"quality\":" << in_data->quality
      << ",\"downsample_x_num\":" << in_data->downsample_x.num << ",\"downsample_x_den\":" << in_data->downsample_x.den
      << ",\"downsample_y_num\":" << in_data->downsample_y.num << ",\"downsample_y_den\":" << in_data->downsample_y.den
      << ",\"layout\":" << int(layout(in_data,out_data,input)) << ",\"checkout_error\":" << err
      << ',' << vv::probe::imageFields(current,other) << '}'; vv::probe::log(s.str());
  }
  if(mode==0) return PF_COPY(input,output,nullptr,nullptr);
  if(err) return err;
  const vv::Image result=vv::probe::display(current,other,mode,p[Gain]->u.fs_d.value);
  if(result.width!=output->width || result.height!=output->height) return PF_Err_INVALID_CALLBACK;
  vv::writePixels(result,output->data,output->rowbytes,layout(in_data,out_data,output),input->data,input->rowbytes);
  return PF_Err_NONE;
}
}
extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr p,PF_PluginDataCB2 cb,SPBasicSuite*,const char*,const char*){
  PF_Err result=PF_Err_INVALID_CALLBACK;
  result=PF_REGISTER_EFFECT_EXT2(p,cb,kName,kMatch,"Venosta Visuals Research",AE_RESERVED_INFO,"EffectMain","https://venostavisuals.com");
  return result;
}
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd,PF_InData* in_data,PF_OutData* out_data,PF_ParamDef* params[],PF_LayerDef* output,void* extra){
  try{
    switch(cmd){
      case PF_Cmd_ABOUT: PF_SPRINTF(out_data->return_msg,"%s\rResearch-only temporal access probe; not a denoiser.",kName); return PF_Err_NONE;
      case PF_Cmd_GLOBAL_SETUP:return setup(in_data,out_data);
      case PF_Cmd_PARAMS_SETUP:return parameters(in_data,out_data);
      case PF_Cmd_USER_CHANGED_PARAM:return user(in_data,out_data,params,extra);
      case PF_Cmd_RENDER:return render(in_data,out_data,params,output);
      default:return PF_Err_NONE;
    }
  }catch(const std::bad_alloc&){return PF_Err_OUT_OF_MEMORY;}catch(...){return PF_Err_INVALID_CALLBACK;}
}
