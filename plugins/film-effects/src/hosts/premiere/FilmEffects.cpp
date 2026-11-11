// Film Effects — CPU (software) entry point, After Effects effect API as hosted by Premiere Pro.
//
// STATUS: NOT YET COMPILED. Written against the After Effects SDK effect API and the Premiere
// "AE effects in Premiere" extensions as documented in:
//   - After Effects SDK guide: effect-basics (PF_Cmd_*, PF_InData), intro/pipl-resources,
//     ppro/bigger-differences (PF Pixel Format Suite, AddSupportedPixelFormat)
//   - Premiere Pro SDK guide: universals/pixel-formats-and-color-spaces (BGRA_4444_8u/32f)
// Reconcile include names and suite versions with the SDK_ProcAmp sample of the SDK you build
// with. All processing is delegated to the host-independent core (src/core), which is tested.

#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHelper.h"
#include "Param_Utils.h"
#include "PrSDKAESupport.h"
#include "entry.h"

#include <cstring>
#include <new>

#include "FilmEffects.h"
#include "FilmEffectsParams.h"
#include "color/pixel_io.h"
#include "pipeline/pipeline.h"

namespace {

bool isPremiere(const PF_InData* in_data) { return in_data->appl_id == 'PrMr'; }

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
  PF_SPRINTF(out_data->return_msg,
             "%s v%d.%d\rBloom, Halation and Film Grain.\rFree, no warranty. venostavisuals.com",
             FE_NAME, FE_MAJOR_VERSION, FE_MINOR_VERSION);
  return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
  out_data->my_version = PF_VERSION(FE_MAJOR_VERSION, FE_MINOR_VERSION, FE_BUG_VERSION, FE_STAGE_VERSION,
                                    FE_BUILD_VERSION);
  // Must match AE_Effect_Global_OutFlags / _2 in FilmEffectsPiPL.r.
  out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
  out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

  if (isPremiere(in_data)) {
    // Premiere: render natively in 32-bit float BGRA (preferred, keeps super-whites) and 8-bit BGRA.
    AEFX_SuiteScoper<PF_PixelFormatSuite1> pixelFormatSuite(in_data, kPFPixelFormatSuite,
                                                            kPFPixelFormatSuiteVersion1, out_data);
    (*pixelFormatSuite->ClearSupportedPixelFormats)(in_data->effect_ref);
    (*pixelFormatSuite->AddSupportedPixelFormat)(in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
    (*pixelFormatSuite->AddSupportedPixelFormat)(in_data->effect_ref, PrPixelFormat_BGRA_4444_8u);
  }
  return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_ParamDef def;
  const fe::FilmParams d;  // defaults come from the core

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUPX("Preset", 8, 1, FE_PRESET_ITEMS, PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, ID_PRESET);

  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Master Enable", d.masterEnable, 0, ID_MASTER_ENABLE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Global Mix", 0, 100, 0, 100, d.mix * 100.0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT,
                       0, ID_MIX);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUPX("Working Space", 3, 1, FE_SPACE_ITEMS, PF_ParamFlag_CANNOT_TIME_VARY, ID_WORKING_SPACE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUPX("Processing Quality", 2, 1, FE_QUALITY_ITEMS, PF_ParamFlag_CANNOT_TIME_VARY, ID_QUALITY);

  // ---- Bloom ----
  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPICX("Bloom", PF_ParamFlag_START_COLLAPSED, ID_BLOOM_GROUP);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Bloom Enable", d.bloom.enabled, 0, ID_BLOOM_ENABLE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Intensity", 0, 1, 0, 1, d.bloom.intensity, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0,
                       ID_BLOOM_INTENSITY);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Threshold", 0, 16, 0, 2, d.bloom.threshold, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE,
                       0, ID_BLOOM_THRESHOLD);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Soft Knee", 0.001, 4, 0.01, 1, d.bloom.knee, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE,
                       0, ID_BLOOM_KNEE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Radius (% of height)", 0.05, 25, 0.5, 15, d.bloom.radius, PF_Precision_TENTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_BLOOM_RADIUS);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(ID_BLOOM_GROUP_END);

  // ---- Halation ----
  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPICX("Halation", PF_ParamFlag_START_COLLAPSED, ID_HAL_GROUP);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Halation Enable", d.halation.enabled, 0, ID_HAL_ENABLE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Intensity", 0, 1, 0, 1, d.halation.intensity, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_HAL_INTENSITY);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Threshold", 0, 16, 0, 2, d.halation.threshold, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_HAL_THRESHOLD);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Radius (% of height)", 0.05, 10, 0.1, 3, d.halation.radius, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_HAL_RADIUS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Warmth (red to orange)", 0, 1, 0, 1, d.halation.warmth, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_HAL_WARMTH);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(ID_HAL_GROUP_END);

  // ---- Grain ----
  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPICX("Film Grain", PF_ParamFlag_START_COLLAPSED, ID_GRAIN_GROUP);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Grain Enable", d.grain.enabled, 0, ID_GRAIN_ENABLE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Amount", 0, 1, 0, 1, d.grain.amount, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0,
                       ID_GRAIN_AMOUNT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Size (px @1080)", 0.5, 8, 0.5, 4, d.grain.size, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_GRAIN_SIZE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Roughness", 0, 1, 0, 1, d.grain.roughness, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE,
                       0, ID_GRAIN_ROUGHNESS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Shadow Response", 0, 2, 0, 2, d.grain.shadows, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_GRAIN_SHADOWS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Midtone Response", 0, 2, 0, 2, d.grain.midtones, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_GRAIN_MIDTONES);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Highlight Response", 0, 2, 0, 2, d.grain.highlights, PF_Precision_HUNDREDTHS,
                       PF_ValueDisplayFlag_NONE, 0, ID_GRAIN_HIGHLIGHTS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUPX("Color Mode", 2, 1, FE_GRAIN_COLOR_ITEMS, 0, ID_GRAIN_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Color Amount", 0, 1, 0, 1, d.grain.chroma, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE,
                       0, ID_GRAIN_CHROMA);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Seed", 0, 99999, 0, 999, d.grain.seed, ID_GRAIN_SEED);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Animate", d.grain.animate, 0, ID_GRAIN_ANIMATE);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(ID_GRAIN_GROUP_END);

  out_data->num_params = FE_NUM_PARAMS;
  return PF_Err_NONE;
}

struct PFParamReader : fe::ParamReader {
  PF_ParamDef** params;
  explicit PFParamReader(PF_ParamDef** p) : params(p) {}
  double number(int i) const override {
    if (params[i]->param_type == PF_Param_SLIDER) return params[i]->u.sd.value;
    return params[i]->u.fs_d.value;
  }
  bool flag(int i) const override { return params[i]->u.bd.value != 0; }
  int popup(int i) const override { return params[i]->u.pd.value; }
};

// Applying a preset writes the preset's values into the visible parameters (so every value stays
// a normal, keyframeable, project-saved parameter) and resets the popup.
PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                        const PF_UserChangedParamExtra* extra) {
  if (extra->param_index != FE_PRESET) return PF_Err_NONE;
  const int choice = params[FE_PRESET]->u.pd.value;  // 1 = "(choose to apply)"
  static const char* kNames[] = {nullptr, "Subtle Film", "Soft Highlight Glow", "Night Lights", "Fine 35mm Grain",
                                 "Coarse 16mm Grain", "Halation Only", "Off"};
  if (choice < 2 || choice > 8) return PF_Err_NONE;
  const fe::Preset* preset = fe::findPreset(kNames[choice - 1]);
  if (!preset) return PF_Err_NONE;
  const fe::FilmParams& p = preset->params;

  auto setF = [&](int i, double v) {
    params[i]->u.fs_d.value = v;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  };
  auto setB = [&](int i, bool v) {
    params[i]->u.bd.value = v ? 1 : 0;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  };
  auto setP = [&](int i, int v) {
    params[i]->u.pd.value = v;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  };
  setB(FE_BLOOM_ENABLE, p.bloom.enabled);
  setF(FE_BLOOM_INTENSITY, p.bloom.intensity);
  setF(FE_BLOOM_THRESHOLD, p.bloom.threshold);
  setF(FE_BLOOM_KNEE, p.bloom.knee);
  setF(FE_BLOOM_RADIUS, p.bloom.radius);
  setB(FE_HAL_ENABLE, p.halation.enabled);
  setF(FE_HAL_INTENSITY, p.halation.intensity);
  setF(FE_HAL_THRESHOLD, p.halation.threshold);
  setF(FE_HAL_RADIUS, p.halation.radius);
  setF(FE_HAL_WARMTH, p.halation.warmth);
  setB(FE_GRAIN_ENABLE, p.grain.enabled);
  setF(FE_GRAIN_AMOUNT, p.grain.amount);
  setF(FE_GRAIN_SIZE, p.grain.size);
  setF(FE_GRAIN_ROUGHNESS, p.grain.roughness);
  setF(FE_GRAIN_SHADOWS, p.grain.shadows);
  setF(FE_GRAIN_MIDTONES, p.grain.midtones);
  setF(FE_GRAIN_HIGHLIGHTS, p.grain.highlights);
  setP(FE_GRAIN_COLOR, p.grain.color == fe::GrainColor::Color ? 2 : 1);
  setF(FE_GRAIN_CHROMA, p.grain.chroma);
  setP(FE_PRESET, 1);
  out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
  (void)in_data;
  return PF_Err_NONE;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_EffectWorld* input = &params[FE_INPUT]->u.ld;
  const fe::FilmParams p = fe::readParams(PFParamReader(params));

  // Bypass: exact copy, no colour conversion at all.
  if (!p.anyEffectActive()) return PF_COPY(input, output, nullptr, nullptr);

  // Determine the buffer layout.
  fe::PixelLayout layout = fe::PixelLayout::ARGB_8u;
  if (isPremiere(in_data)) {
    AEFX_SuiteScoper<PF_PixelFormatSuite1> pfs(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
    PrPixelFormat format = PrPixelFormat_BGRA_4444_8u;
    (*pfs->GetPixelFormat)(output, &format);
    if (format == PrPixelFormat_BGRA_4444_32f) layout = fe::PixelLayout::BGRA_32f;
    else if (format == PrPixelFormat_BGRA_4444_8u) layout = fe::PixelLayout::BGRA_8u;
    else return PF_Err_INVALID_CALLBACK;  // only the formats registered in GlobalSetup are expected
  } else {
    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    PF_PixelFormat format = PF_PixelFormat_ARGB32;
    ws->PF_GetPixelFormat(output, &format);
    if (format == PF_PixelFormat_ARGB128) layout = fe::PixelLayout::ARGB_32f;
    else if (format == PF_PixelFormat_ARGB64) layout = fe::PixelLayout::ARGB_16u;
  }

  fe::Image img;
  fe::readPixels(input->data, input->rowbytes, input->width, input->height, layout, img);

  fe::RenderContext ctx;
  ctx.frameKey = fe::frameKeyFromTime(in_data->current_time, in_data->time_step);
  fe::process(img, p, ctx);

  if (output->width != img.width || output->height != img.height) return PF_COPY(input, output, nullptr, nullptr);
  fe::writePixels(img, output->data, output->rowbytes, layout, input->data, input->rowbytes);
  return PF_Err_NONE;
}

}  // namespace

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* inSPBasicSuitePtr, const char* inHostName,
                                                     const char* inHostVersion) {
  (void)inSPBasicSuitePtr;
  (void)inHostName;
  (void)inHostVersion;
  return PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, FE_NAME, FE_MATCH_NAME, FE_CATEGORY,
                                 AE_RESERVED_INFO, "EffectMain", "https://venostavisuals.com");
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* output, void* extra) {
  PF_Err err = PF_Err_NONE;
  try {
    switch (cmd) {
      case PF_Cmd_ABOUT: err = About(in_data, out_data); break;
      case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data); break;
      case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
      case PF_Cmd_USER_CHANGED_PARAM:
        err = UserChangedParam(in_data, out_data, params, static_cast<const PF_UserChangedParamExtra*>(extra));
        break;
      case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
      default: break;
    }
  } catch (const std::bad_alloc&) {
    err = PF_Err_OUT_OF_MEMORY;
  } catch (...) {
    err = PF_Err_INTERNAL_STRUCT_DAMAGED;
  }
  return err;
}
