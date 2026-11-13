"""End-to-end dataset/scoring contract and destructive-control qualification, stdlib only."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

tool = sys.argv[1]


def run(*args, ok=True):
    p = subprocess.run([tool, *map(str, args)], capture_output=True, text=True)
    if ok:
        assert p.returncode == 0, p.stderr
    else:
        assert p.returncode != 0, (args, p.stdout)
    return p


def mean(rows, method, key):
    values = [r[key]["value"] for r in rows if r["method"] == method and r[key]["value"] is not None]
    assert values, (method, key)
    return sum(values) / len(values)


with tempfile.TemporaryDirectory(prefix="dn-sequence-cli-") as tmp:
    root = Path(tmp)
    report = root / "controls.json"
    run("benchmark", "--width", 96, "--height", 64, "--frames", 6, "--seeds", 7, "--output", report)
    data = json.loads(report.read_text())
    rows = data["rows"]
    assert len(rows) == 5 * 6 * 5
    assert all(r["rgb_rmse"]["value"] == 0 for r in rows if r["method"] == "clean-oracle")
    assert math.isclose(mean(rows, "clean-oracle", "detail_transfer"), 1.0)
    oracle = next(r for r in rows if r["method"] == "clean-oracle" and r["scenario"] == "static-detail")
    for region in ("skin", "fabric", "foliage", "hair"):
        assert math.isclose(oracle["detail_regions"][region]["detail_transfer"]["value"], 1.0)
    assert oracle["detail_regions"]["foreground"]["rgb_rmse"]["value"] is None
    assert mean(rows, "blur-0.8", "detail_transfer") < 0.8
    moving = [r for r in rows if r["scenario"] in ("subpixel-pan", "moving-occluder") and r["frame"] > 0]
    assert mean(moving, "frozen-clean", "rgb_rmse") > 0.001
    assert mean(moving, "unaligned-temporal", "disocclusion_rmse") > 0.001
    cuts = [r for r in rows if r["cut"]]
    assert cuts and all(r["temporal_residual_rmse"] == {"value": None, "pixels": 0} for r in cuts)
    assert mean(cuts, "unaligned-temporal", "rgb_rmse") > 0.001

    # Export one versioned dataset. Clean output is an exact external oracle roundtrip;
    # the scorer must use the persisted reference, flow and masks, not regenerate new truth.
    dataset, candidate = root / "dataset", root / "candidate"
    run("export", "--width", 96, "--height", 64, "--frames", 6,
        "--scenario", "moving-occluder", "--seeds", 19, "--output", dataset)
    manifest = json.loads((dataset / "manifest.json").read_text())
    assert "curve only" in manifest["encoding"] and manifest["seed"] == 19
    candidate.mkdir()
    for frame in range(6):
        shutil.copyfile(dataset / f"clean-{frame:04d}.pfm", candidate / f"output-{frame:04d}.pfm")
    scored = json.loads(run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle").stdout)
    assert len(scored["rows"]) == 6
    assert all(r["rgb_rmse"]["value"] == 0 for r in scored["rows"])
    assert all(r["temporal_residual_rmse"]["value"] in (None, 0) for r in scored["rows"])
    assert scored["dataset_source_revision"] == manifest["dataset_source_revision"]
    assert scored["candidate_name"] == "clean-oracle"
    assert all(r["output_rgb_fnv1a64"] == r["clean_rgb_fnv1a64"] for r in scored["rows"])
    config = dataset / "dataset.conf"
    original_config = config.read_bytes()
    config.write_bytes(original_config.replace(b"seed 19\n", b"seed 20\n"))
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)
    config.write_bytes(original_config)
    # Independently transcode one PFM payload to big endian, retaining its asymmetric row data.
    first_output = candidate / "output-0000.pfm"
    pfm = first_output.read_bytes()
    magic, dims, scale, payload = pfm.split(b"\n", 3)
    values = struct.unpack("<" + "f" * (len(payload) // 4), payload)
    first_output.write_bytes(magic + b"\n" + dims + b"\n1.0\n" + struct.pack(">" + "f" * len(values), *values))
    big = json.loads(run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "big-endian-oracle").stdout)
    assert big["rows"][0]["rgb_rmse"]["value"] == 0
    first_output.write_bytes(pfm)

    # Different stored RGB values are detected even when dimensions/header remain valid.
    reference = dataset / "clean-0000.pfm"
    original = reference.read_bytes()
    corrupted = bytearray(original)
    corrupted[-1] ^= 1
    reference.write_bytes(corrupted)
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)
    reference.write_bytes(original)
    # Candidate decode rejects NaN, truncated payloads, wrong dimensions, missing frames.
    output = candidate / "output-0000.pfm"
    raw = output.read_bytes()
    output.write_bytes(raw[:-4] + struct.pack("<f", float("nan")))
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)
    output.write_bytes(raw[:-1])
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)
    output.write_bytes(b"PF\n1 1\n-1\n" + struct.pack("<fff", 0.0, 0.0, 0.0))
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)
    output.unlink()
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "clean-oracle", ok=False)

    # A directory containing prior results is never silently overwritten.
    run("export", "--scenario", "static-detail", "--seeds", 7, "--output", dataset, ok=False)
    run("benchmark", "--width", -1, ok=False)
    run("benchmark", "--frames", "8x", ok=False)
    run("benchmark", "--seeds", "7,7", ok=False)
    run("benchmark", "--read-noise", "nan", ok=False)
    run("benchmark", "--scenario", "unknown", ok=False)
    run("score", "--dataset", dataset, "--candidate-dir", candidate, "--candidate-name", "bad-options", "--exposure", 0, ok=False)

print("Sequence CLI controls, dataset fingerprints and external scoring verified.")
