// Shared Premiere Pro adapter, CPU (software) path: the After Effects effect API as hosted by
// Premiere Pro (also loads in After Effects), for any vv::Effect.
//
// Registration (PluginDataEntryFunction2), parameters from the plugin's table (PF_Cmd_PARAMS_SETUP,
// on-disk IDs from the table), supervised parameters (presets) through Effect::paramChanged,
// Premiere pixel formats BGRA 8u / 32f (After Effects ARGB 8/16/32), bit-exact bypass.
// Written against the AE / Premiere SDK documentation and checked against the SDK samples in CI
// (the SDK is not in this repository). PiPL: vv_pipl.r. Version in GlobalSetup and PiPL must match.
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

#include "vv/effect.h"
#include "vv/pixel_io.h"

namespace {

const vv::Effect& E() { return vv::effect(); }

bool isPremiere(const PF_InData* in_data) { return in_data->appl_id == 'PrMr'; }

// Must equal AE_Effect_Version in vv_pipl.r: PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1).
constexpr A_long kPluginVersion = PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1);

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
  const vv::EffectInfo& info = E().info();
  PF_SPRINTF(out_data->return_msg, "%s v%d.%d\r%s\rFree, no warranty. venostavisuals.com", info.name,
             info.versionMajor, info.versionMinor, info.description);
  return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
  out_data->my_version = kPluginVersion;
  // Must match AE_Effect_Global_OutFlags / _2 in vv_pipl.r.
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
  const auto& specs = E().params();
  for (const vv::ParamSpec& s : specs) {
    PF_ParamFlags flags = 0;
    if (s.supervise) flags |= PF_ParamFlag_SUPERVISE;
    if (s.cannotTimeVary) flags |= PF_ParamFlag_CANNOT_TIME_VARY;
    if (s.startCollapsed) flags |= PF_ParamFlag_START_COLLAPSED;
    AEFX_CLR_STRUCT(def);
    switch (s.kind) {
      case vv::ParamKind::GroupStart:
        PF_ADD_TOPICX(s.name, flags, s.diskId);
        break;
      case vv::ParamKind::GroupEnd:
        PF_END_TOPIC(s.diskId);
        break;
      case vv::ParamKind::Checkbox:
        PF_ADD_CHECKBOXX(s.name, s.dflt != 0.0, flags, s.diskId);
        break;
      case vv::ParamKind::Float:
        PF_ADD_FLOAT_SLIDERX(s.name, s.validMin, s.validMax, s.sliderMin, s.sliderMax, s.dflt,
                             s.precision == 1 ? PF_Precision_TENTHS : PF_Precision_HUNDREDTHS,
                             s.percent ? PF_ValueDisplayFlag_PERCENT : PF_ValueDisplayFlag_NONE, flags, s.diskId);
        break;
      case vv::ParamKind::Popup:
        PF_ADD_POPUPX(s.name, s.itemCount, static_cast<int>(s.dflt), s.items, flags, s.diskId);
        break;
      case vv::ParamKind::IntSlider:
        PF_ADD_SLIDER(s.name, static_cast<int>(s.validMin), static_cast<int>(s.validMax),
                      static_cast<int>(s.sliderMin), static_cast<int>(s.sliderMax), static_cast<int>(s.dflt),
                      s.diskId);
        break;
    }
  }
  out_data->num_params = static_cast<A_long>(specs.size()) + 1;  // + input layer
  return PF_Err_NONE;
}

struct PFParamReader : vv::ParamReader {
  PF_ParamDef** params;
  explicit PFParamReader(PF_ParamDef** p) : params(p) {}
  double number(int i) const override {
    if (params[i]->param_type == PF_Param_SLIDER) return params[i]->u.sd.value;
    return params[i]->u.fs_d.value;
  }
  bool flag(int i) const override { return params[i]->u.bd.value != 0; }
  int popup(int i) const override { return params[i]->u.pd.value; }
};

struct PFParamWriter : vv::ParamWriter {
  PF_ParamDef** params;
  explicit PFParamWriter(PF_ParamDef** p) : params(p) {}
  void setNumber(int i, double v) override {
    if (params[i]->param_type == PF_Param_SLIDER) params[i]->u.sd.value = static_cast<A_long>(v);
    else params[i]->u.fs_d.value = v;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  }
  void setFlag(int i, bool v) override {
    params[i]->u.bd.value = v ? 1 : 0;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  }
  void setPopup(int i, int v) override {
    params[i]->u.pd.value = v;
    params[i]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  }
};

PF_Err UserChangedParam(PF_OutData* out_data, PF_ParamDef* params[], const PF_UserChangedParamExtra* extra) {
  PFParamReader r(params);
  PFParamWriter w(params);
  if (E().paramChanged(static_cast<int>(extra->param_index), r, w))
    out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
  return PF_Err_NONE;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_EffectWorld* input = &params[0]->u.ld;
  const PFParamReader reader(params);

  // Bypass: exact copy, no colour conversion at all.
  if (!E().active(reader)) return PF_COPY(input, output, nullptr, nullptr);

  // Determine the buffer layout.
  vv::PixelLayout layout = vv::PixelLayout::ARGB_8u;
  if (isPremiere(in_data)) {
    AEFX_SuiteScoper<PF_PixelFormatSuite1> pfs(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
    PrPixelFormat format = PrPixelFormat_BGRA_4444_8u;
    (*pfs->GetPixelFormat)(output, &format);
    if (format == PrPixelFormat_BGRA_4444_32f) layout = vv::PixelLayout::BGRA_32f;
    else if (format == PrPixelFormat_BGRA_4444_8u) layout = vv::PixelLayout::BGRA_8u;
    else return PF_Err_INVALID_CALLBACK;  // only the formats registered in GlobalSetup are expected
  } else {
    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    PF_PixelFormat format = PF_PixelFormat_ARGB32;
    ws->PF_GetPixelFormat(output, &format);
    if (format == PF_PixelFormat_ARGB128) layout = vv::PixelLayout::ARGB_32f;
    else if (format == PF_PixelFormat_ARGB64) layout = vv::PixelLayout::ARGB_16u;
  }

  vv::Image img;
  vv::readPixels(input->data, input->rowbytes, input->width, input->height, layout, img);
  E().render(img, reader, vv::frameKeyFromTime(in_data->current_time, in_data->time_step));

  if (output->width != img.width || output->height != img.height) return PF_COPY(input, output, nullptr, nullptr);
  vv::writePixels(img, output->data, output->rowbytes, layout, input->data, input->rowbytes);
  return PF_Err_NONE;
}

}  // namespace

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* inSPBasicSuitePtr, const char* inHostName,
                                                     const char* inHostVersion) {
  (void)inSPBasicSuitePtr;
  (void)inHostName;
  (void)inHostVersion;
  const vv::EffectInfo& info = E().info();
  // The SDK macro assigns to a local named `result` (as in the SDK_Noise sample).
  PF_Err result = PF_Err_INVALID_CALLBACK;
  result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, info.name, info.matchName, info.category,
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
        err = UserChangedParam(out_data, params, static_cast<const PF_UserChangedParamExtra*>(extra));
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
