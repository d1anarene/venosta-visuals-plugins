// Denoise evaluation — image quality metrics against ground truth (RGB, alpha ignored).
#pragma once
#include "vv/image.h"

namespace dn {
namespace eval {

/** PSNR in dB for peak 1.0 over the RGB channels. */
double psnr(const vv::Image& a, const vv::Image& ref);

/** Mean SSIM (Wang et al. 2004) on Rec.709 luma, Gaussian window sigma 1.5, K1 0.01, K2 0.03. */
double ssim(const vv::Image& a, const vv::Image& ref);

}  // namespace eval
}  // namespace dn
