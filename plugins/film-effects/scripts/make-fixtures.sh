#!/usr/bin/env bash
# Generate the synthetic fixture scenes and A/B renders into tests/fixtures/out (not committed).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build --parallel >/dev/null
OUT=tests/fixtures/out; mkdir -p "$OUT"
for scene in point-lights window neon gradient chart night-street foliage midgrey; do
  for preset in "Subtle Film" "Soft Highlight Glow" "Night Lights" "Fine 35mm Grain" "Coarse 16mm Grain" "Halation Only"; do
    tag=$(echo "$preset" | tr ' ' '_')
    ./build/fe_render --scene "$scene" --preset "$preset" --before "$OUT/${scene}_input.ppm" --out "$OUT/${scene}_${tag}.ppm"
  done
done
echo "Wrote $(ls "$OUT" | wc -l) images to $OUT (PPM, working encoding)."
