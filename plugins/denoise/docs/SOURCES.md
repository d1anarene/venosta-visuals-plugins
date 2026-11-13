# Denoise — primary sources and reference-use register

_Reviewed 2026-11-13. These are published methods/API contracts, not Denoise measurements._

## Filtering, noise models and benchmarks

| Source | Relevance and limit | Intended use / current permission state |
| --- | --- | --- |
| [Arias & Morel, Video Denoising via Empirical Bayesian Estimation of Space-Time Patches](https://dev.ipol.im/~pariasm/video_nlbayes/vnlb_files/vnlb_preprint.pdf) | Strong classical space-time patch reference; white-Gaussian assumptions do not validate processed log noise. No universal leadership ranking follows. | Maths/quality baseline candidate; review exact implementation terms before running |
| [Ehret & Arias, VBM3D implementation and variants, IPOL 2021](https://www.ipol.im/pub/art/2021/340/) | Reproducible classical video comparison, including motion-guided search variants. | Separate offline reference candidate; [author code](https://github.com/tehret/vbm3d) lists GPLv3+; do not incorporate code into this MIT plugin |
| [Mäkinen, Azzari & Foi, Collaborative Filtering of Correlated Noise, TIP 2020](https://researchportal.tuni.fi/en/publications/collaborative-filtering-of-correlated-noise-exact-transform-domai/) | Transform-domain noise variance/correlation matters for collaborative filtering. | Independently implemented maths; compare with scalar per-band approximations |
| [Jin et al., Classic Video Denoising in a Machine Learning World, CVPR 2025](https://openaccess.thecvf.com/content/CVPR2025/html/Jin_Classic_Video_Denoising_in_a_Machine_Learning_World_Robust_Fast_CVPR_2025_paper.html) | Contemporary hybrid: classical filtering with learned local parameters. Relevant to controllability and varied noise. | Architecture/benchmark reference; [authors report source unavailable](https://srameo.github.io/projects/levd/). No dependency or transferred speed claim |
| [Foi et al., Practical Poissonian-Gaussian Noise Modeling and Fitting](https://webpages.tuni.fi/foi/sensornoise.html) | Sensor-domain signal dependence; processed/log images need additional validation. | Theory source; do not assume downloadable software has unrestricted use |
| [Mäkitalo & Foi, generalized Anscombe inverse resources](https://webpages.tuni.fi/foi/invansc/) | Inversion results apply under stated noise/transform assumptions. | Theory source; does not establish an unbiased inverse for arbitrary fitted log-video profiles |
| [Liu, Tanaka & Okutomi, noise-estimation resources](https://www.ok.sc.e.titech.ac.jp/~mtanaka/proj/noise/) | Weak-texture patch selection and noise estimation. | Candidate estimator research; compare against texture-only and correlated-noise failures |
| [Kroeger et al., DIS optical flow, ECCV 2016](https://arxiv.org/abs/1603.03590) | Fast motion-estimation candidate. | Maths only; accuracy on noisy log imagery must be measured independently |
| [Wang et al., SSIM authors' resources](https://ece.uwaterloo.ca/~z70wang/research/ssim/) | Frame structure metric and reference conventions. | Validate our implementation; SSIM alone does not assess motion or chroma |
| [FastDVDnet author repository](https://github.com/m-tassano/fastdvdnet) | Speed-oriented learned comparator, trained for particular synthetic noise domains. | MIT code as reviewed; separately record checkpoint provenance and dependency/data terms before offline use; no v1 inference dependency |
| [RVRT author repository](https://github.com/JingyunLiang/RVRT) | Heavier learned quality comparator; Gaussian benchmarks and tiling choices do not establish camera-log quality. | CC-BY-NC code: eligibility for this project remains unresolved; not used/downloaded in this update |
| [PVDD authors' paper](https://arxiv.org/abs/2207.01356) | Dynamic paired sequences with calibrated RAW-noise/ISP synthesis. Not a native camera-log validation set. | Dataset permission review pending; not acquired |
| [CRVD / RViDeNet official repository](https://github.com/cao-cong/RViDeNet) | Captured RAW video and averaged reference data; useful sensor-noise evidence. | Academic-only / CC BY-NC-SA 4.0 restrictions stated by repository: project-use eligibility unresolved; not acquired |
| [AIM 2025 low-light RAW video challenge](https://openaccess.thecvf.com/content/ICCV2025W/AIM/html/Yakovenko_AIM_2025_Low-light_RAW_Video_Denoising_Challenge_Dataset_Methods_and_ICCVW_2025_paper.html) | Broader low-light sensor benchmark; last-frame RAW output does not prove continuous-video stability or log support. | Auxiliary research; dataset permission review pending; not acquired |

Original [Tampere BM3D/BM4D/V-BM4D software](https://webpages.tuni.fi/foi/GCF-BM3D/) has usage
restrictions. The plan's earlier "offline only" wording is not permission: free distribution of
our plugin does not automatically authorize every reference package or dataset. Keep paper-based
independent implementations and external reference runners separate. Before a runner is used,
record exact version/commit, code license, model/data terms, allowed purpose and configuration.
No reference implementation code or weights were copied or added during this update.

## Host integration

| Source | Contract to verify in a probe |
| --- | --- |
| [Premiere SDK guide: GPU function descriptions](https://ppro-plugins.docsforadobe.dev/gpu-effects-transitions/function-descriptions/) and [structures](https://ppro-plugins.docsforadobe.dev/gpu-effects-transitions/structure-descriptions/) | Frame dependencies, delivered frame ordering and render quality fields. A documented dependency API is not proof of upstream-stage consistency or export detection. |
| [AE SDK guide: interaction callbacks](https://ae-plugins.docsforadobe.dev/effect-details/interaction-callback-functions/) | Ordinary layer checkout versus current input can differ in preceding-effects treatment. A successful fetch alone is insufficient. |
| [AE SDK guide: Premiere differences](https://ae-plugins.docsforadobe.dev/ppro/bigger-differences/) | Premiere does not provide AE SmartFX; verify pixel-format checkout and actual CPU temporal input stage. |
| [AE SDK guide: parameter supervision](https://ae-plugins.docsforadobe.dev/effect-details/parameter-supervision/) | User-change actions and legal profile persistence; Premiere-specific button/image behaviour remains untested. |
| [OpenFX images and clips](https://openfx.readthedocs.io/en/latest/Reference/ofxImageClip.html) | Host, effect and source temporal capability; valid image-fetch actions and image lifetime. |
| [OpenFX rendering reference](https://github.com/AcademySoftwareFoundation/openfx/blob/main/Documentation/sources/Reference/ofxRendering.rst) | Frames-needed declarations, render contexts and quality semantics; Resolve behaviour must be observed. |
| [OpenFX parameter suite](https://openfx.readthedocs.io/en/main/Reference/suites/ofxParametersSuiteReference.html) | Update persistent profile parameters in permitted editing actions, not ordinary render. |

The private Premiere 27.0 header findings in [RESEARCH.md](RESEARCH.md) remain a dated local
record; SDK headers/samples must not be committed. Online documentation is not a replacement for
compilation against the legitimate SDK version and an actual host test.

## Product comparison sources

- [Neat Video v6 Premiere guide](https://www.neatvideo.com/user-guides/nv6/NVPRUG.pdf): profiling,
  temporal/spatial filtering and quality controls. Use a valid installation and record version,
  profile and settings when making comparisons; feature documentation is not independent proof
  of comparative quality.
- [Neat Video profiling guidance](https://www.neatvideo.com/support/how-to-use/device-noise-profiling)
  and [FAQ](https://www.neatvideo.com/support/faq): profile suitability and overfiltering are
  important comparison controls. Do not compare a tuned candidate against a deliberately bad profile.
- [Blackmagic Design Resolve reference manual](https://documents.blackmagicdesign.com/UserManuals/DaVinciResolveReferenceManual.pdf):
  record the installed Studio version and exact noise-reduction modes when testing. Do not infer
  performance or availability on the owner's machine from the manual.

No commercial-reference results exist in this repository yet. Proprietary implementations remain
black-box comparators; none of their code, assets or UI is part of our design.
