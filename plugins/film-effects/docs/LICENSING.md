# Licensing

- Film Effects source code: MIT (repository `LICENSE`). Free, no warranty.
- DaVinci Resolve / OpenFX build: uses the OpenFX headers and C++ Support library (Academy
  Software Foundation, BSD-3-Clause, https://github.com/AcademySoftwareFoundation/openfx). They
  are downloaded at build time (pinned commit) and compiled into `FilmEffects.ofx`; they are not
  stored in this repository. BSD-3-Clause allows redistribution in binary form with its notice.
- Algorithms written for this project from general image-processing principles (soft-knee
  thresholds, Gaussian pyramids, hashed value noise). No Dehancer or other proprietary code, assets
  or names are used.
- `fe_hash` uses the public-domain "lowbias32" integer hash by Chris Wellons.
- Adobe SDK headers/samples are **not** included. You must download the After Effects SDK and the
  Premiere Pro SDK yourself (Adobe Developer Console) and accept Adobe's SDK licence to build the
  native plugin.
- Buckswood post plugins (MIT) were reviewed for research only; no code was copied.
- All test images are generated procedurally by `tools/fe_render` and may be redistributed.

- Windows CUDA binaries statically link NVIDIA's CUDA Runtime, which is listed in the Toolkit's
  redistributable components. NVIDIA components retain their own terms, separate from the MIT
  license of this project's source. The CUDA ZIP includes `NVIDIA-CUDA-EULA.html` from the
  [CUDA 12.9.1 license and third-party notices](https://docs.nvidia.com/cuda/archive/12.9.1/eula/index.html).
  CUDA compiler tools, Adobe SDK headers and SDK samples are not included in the download.
