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

constexpr fe::Variant kVariant = static_cast<fe::Variant>(FE_VARIANT);

int idx(fe::ParamKey k) { return fe::paramIndex(kVariant, k); }

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_ParamDef def;
  const auto keys = fe::variantKeys(kVariant);
  for (fe::ParamKey key : keys) {
    const fe::ParamSpec& s = fe::specFor(key);
    PF_ParamFlags flags = 0;
    if (s.supervise) flags |= PF_ParamFlag_SUPERVISE;
    if (s.cannotTimeVary) flags |= PF_ParamFlag_CANNOT_TIME_VARY;
    if (s.startCollapsed) flags |= PF_ParamFlag_START_COLLAPSED;
    AEFX_CLR_STRUCT(def);
    switch (s.kind) {
      case fe::ParamKind::GroupStart:
        PF_ADD_TOPICX(s.name, flags, s.diskId);
        break;
      case fe::ParamKind::GroupEnd:
        PF_END_TOPIC(s.diskId);
        break;
      case fe::ParamKind::Checkbox:
        PF_ADD_CHECKBOXX(s.name, s.dflt != 0.0, flags, s.diskId);
        break;
      case fe::ParamKind::Float:
        PF_ADD_FLOAT_SLIDERX(s.name, s.validMin, s.validMax, s.sliderMin, s.sliderMax, s.dflt,
                             s.precision == 1 ? PF_Precision_TENTHS : PF_Precision_HUNDREDTHS,
                             s.percent ? PF_ValueDisplayFlag_PERCENT : PF_ValueDisplayFlag_NONE, flags, s.diskId);
        break;
      case fe::ParamKind::Popup:
        PF_ADD_POPUPX(s.name, s.itemCount, static_cast<int>(s.dflt), s.items, flags, s.diskId);
        break;
      case fe::ParamKind::IntSlider:
        PF_ADD_SLIDER(s.name, static_cast<int>(s.validMin), static_cast<int>(s.validMax),
                      static_cast<int>(s.sliderMin), static_cast<int>(s.sliderMax), static_cast<int>(s.dflt),
                      s.diskId);
        break;
    }
  }
  out_data->num_params = static_cast<A_long>(keys.size()) + 1;  // + input layer
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

// Applying a preset (combined effect only) writes the preset's values into the visible
// parameters, so every value stays a normal, keyframeable, project-saved parameter.
PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                        const PF_UserChangedParamExtra* extra) {
  (void)in_data;
  using K = fe::ParamKey;
  if (kVariant != fe::Variant::Combined || extra->param_index != idx(K::Preset)) return PF_Err_NONE;
  const int choice = params[idx(K::Preset)]->u.pd.value;  // 1 = "(choose to apply)"
  const char* name = fe::presetMenuName(choice);
  if (!name || choice < 2) return PF_Err_NONE;
  const fe::Preset* preset = fe::findPreset(name);
  if (!preset) return PF_Err_NONE;

  auto setP = [&](K k, int v) {
    params[idx(k)]->u.pd.value = v;
    params[idx(k)]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  };
  fe::forEachPresetValue(preset->params, [&](K k, double v) {
    const int i = idx(k);
    if (i < 0) return;
    switch (fe::specFor(k).kind) {
      case fe::ParamKind::Checkbox: params[i]->u.bd.value = v != 0.0 ? 1 : 0; break;
      case fe::ParamKind::Popup: params[i]->u.pd.value = static_cast<A_long>(v); break;
      case fe::ParamKind::IntSlider: params[i]->u.sd.value = static_cast<A_long>(v); break;
      case fe::ParamKind::Float: params[i]->u.fs_d.value = v; break;
      default: return;
    }
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  });
  setP(K::Preset, 1);
  out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
  return PF_Err_NONE;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_EffectWorld* input = &params[0]->u.ld;
  const fe::FilmParams p = fe::readParams(PFParamReader(params), kVariant);

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
  // The SDK macro assigns to a local named `result` (as in the SDK_Noise sample).
  PF_Err result = PF_Err_INVALID_CALLBACK;
  result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, FE_NAME, FE_MATCH_NAME, FE_CATEGORY,
                                   AE_RESERVED_INFO, "EffectMain", "https://venostavisuals.com");
  return result;
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
