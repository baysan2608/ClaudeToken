#!/usr/bin/env python3
"""Validate the generated Fourfold SFX library.

    python3 tools/audio/validate_sfx.py                      # metrics table + pass/fail
    python3 tools/audio/validate_sfx.py --determinism        # render twice, compare hashes
    python3 tools/audio/validate_sfx.py --spectrograms DIR deflect block lava_wave_loop

Checks per file: format (16-bit mono 44.1 kHz), duration, peak dBFS, RMS, clipping, DC offset,
silence at start/end (one-shots), loop seam (|last-first| and its ratio to the typical sample step,
plus RMS continuity across the seam), manifest agreement.  Exit status is non-zero on any failure.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DEFAULT_DIR = HERE.parents[1] / "game" / "assets" / "audio"
SR = 44100


def load(path: Path):
    with wave.open(str(path), "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    return x, (ch, sw, sr)


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def analyse(path: Path, entry: dict | None):
    x, (ch, sw, sr) = load(path)
    n = len(x)
    xf = x / 32768.0
    out = {"name": path.stem, "n": n, "dur": n / SR, "fail": []}
    if (ch, sw, sr) != (1, 2, SR):
        out["fail"].append(f"format ch={ch} sw={sw} sr={sr}")
    peak = np.max(np.abs(xf))
    out["peak_db"] = db(peak)
    out["rms_db"] = db(np.sqrt(np.mean(xf ** 2)))
    out["clip"] = int(np.sum(np.abs(x) >= 32767))
    out["dc"] = float(np.mean(xf))
    out["dc_rel"] = abs(out["dc"]) / max(np.sqrt(np.mean(xf ** 2)), 1e-9)
    spec = np.abs(np.fft.rfft(xf * np.hanning(n))) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    out["centroid"] = float(np.sum(freqs * spec) / max(np.sum(spec), 1e-12))  # energy-weighted
    loop = bool(entry and entry.get("loop"))
    out["loop"] = loop
    target = -18.0 if path.stem.startswith("amb_") else -3.0
    if abs(out["peak_db"] - target) > 0.25:
        out["fail"].append(f"peak {out['peak_db']:.2f} dBFS, expected about {target}")
    if out["clip"]:
        out["fail"].append(f"{out['clip']} clipped samples")
    if out["dc_rel"] > 0.01 or abs(out["dc"]) > 0.001:
        out["fail"].append(f"DC offset {out['dc']:.5f}")
    if loop:
        if not 1.0 <= out["dur"] <= 3.0:
            out["fail"].append("loop length outside 1-3 s")
        # sample step across the seam (last -> first) vs. every interior step
        steps = np.abs(np.diff(x))
        seam = abs(x[0] - x[-1])
        out["seam_abs"] = seam / 32768.0
        out["seam_x_rms"] = seam / max(float(np.sqrt(np.mean(steps ** 2))), 1.0)
        out["seam_x_max"] = seam / max(float(steps.max()), 1.0)
        if out["seam_x_max"] > 1.25:
            out["fail"].append(f"seam step is {out['seam_x_max']:.2f}x the largest interior step")
        # RMS continuity: level change across the seam (last w ms vs first w ms) ranked against the level
        # change across every other (circularly shifted) position in the loop. A periodic loop's seam is
        # statistically indistinguishable from any interior point; a bad seam ranks at the extreme.
        for ms in (25, 100):
            w = int(ms / 1000 * SR)
            cs = np.concatenate([[0.0], np.cumsum(np.concatenate([xf, xf, xf]) ** 2)])
            shifts = np.arange(0, n, max(w // 8, 1))
            before = np.sqrt((cs[shifts + n] - cs[shifts + n - w]) / w)
            after = np.sqrt((cs[shifts + n + w] - cs[shifts + n]) / w)
            jumps = np.abs(20 * np.log10(np.maximum(before, 1e-9) / np.maximum(after, 1e-9)))
            jump = jumps[0]
            pct = float(np.mean(jumps <= jump) * 100)
            out[f"seam_rms{ms}"] = float(jump)
            out[f"seam_pct{ms}"] = pct
            if pct >= 99.0 and jump > 3.0:
                out["fail"].append(f"RMS jump {jump:.1f} dB across seam ({ms} ms windows, p{pct:.0f} of all positions)")
    else:
        limit = 1.5 if path.stem == "unlock" else 1.2
        if out["dur"] > limit + 1e-6:
            out["fail"].append(f"one-shot longer than {limit} s")
        if path.stem.startswith("element_") and out["dur"] >= 0.35:
            out["fail"].append("element motif must be < 0.35 s")
        if not 0.1 <= out["dur"]:
            out["fail"].append("one-shot shorter than 0.1 s")
        out["head"] = abs(xf[0])
        out["tail"] = abs(xf[-1])
        w = int(0.01 * SR)
        out["tail_db"] = db(np.sqrt(np.mean(xf[-w:] ** 2))) - out["peak_db"]
        if out["head"] > 0.002 or out["tail"] > 0.002:
            out["fail"].append("non-zero first/last sample (click risk)")
        if out["tail_db"] > -40:
            out["fail"].append(f"tail cut off ({out['tail_db']:.0f} dB re peak in last 10 ms)")
    if entry is not None and abs(entry["duration_s"] - out["dur"]) > 0.002:
        out["fail"].append("manifest duration mismatch")
    return out


def sha(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes(d: Path):
    return {p.name: sha(p) for p in sorted(d.glob("*.wav"))} | (
        {"manifest.json": sha(d / "manifest.json")} if (d / "manifest.json").exists() else {})


def determinism():
    synth = HERE / "synth_sfx.py"
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        for d in (a, b):
            subprocess.run([sys.executable, str(synth), "--out", d], check=True, stdout=subprocess.DEVNULL)
        ha, hb = hashes(Path(a)), hashes(Path(b))
    same = ha == hb
    comb = hashlib.sha256("".join(f"{k}:{v}" for k, v in sorted(ha.items())).encode()).hexdigest()
    print(f"determinism: {len(ha)} files, run A == run B: {same}; combined sha256 {comb[:16]}")
    return same


def spectrograms(d: Path, outdir: Path, names):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy import signal
    outdir.mkdir(parents=True, exist_ok=True)
    cols = 2
    rows = (len(names) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(7 * cols, 3.4 * rows), squeeze=False)
    for ax, name in zip(axes.ravel(), names):
        x, _ = load(d / f"{name}.wav")
        f, t, S = signal.spectrogram(x / 32768.0, SR, nperseg=1024, noverlap=896, window="hann")
        ax.pcolormesh(t, f / 1000, 10 * np.log10(S + 1e-12), vmin=-105, vmax=-35, shading="auto", cmap="magma")
        ax.set_title(name)
        ax.set_ylim(0, 16)
        ax.set_ylabel("kHz")
        ax.set_xlabel("s")
    for ax in axes.ravel()[len(names):]:
        ax.axis("off")
    fig.tight_layout()
    path = outdir / ("spec_" + "_".join(names[:3]) + f"_{len(names)}.png")
    fig.savefig(path, dpi=80)
    print("spectrogram:", path)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path, default=DEFAULT_DIR)
    ap.add_argument("--determinism", action="store_true")
    ap.add_argument("--spectrograms", nargs="+", metavar=("OUTDIR", "NAME"))
    ap.add_argument("--hashes", action="store_true", help="print sha256 of every file")
    ap.add_argument("--table", action="store_true", help="print the manifest as a markdown table and exit")
    args = ap.parse_args()
    d = args.dir
    manifest = json.loads((d / "manifest.json").read_text())
    if args.table:
        print("| file | category | loop | length (s) | vol (dB) | voices |")
        print("|---|---|---|---|---|---|")
        for name, e in manifest.items():
            print(f"| `{name}` | {e['category']} | {'yes' if e['loop'] else ''} | {e['duration_s']:.2f} | "
                  f"{e['suggested_volume_db']:g} | {e['max_voices']} |")
        return 0
    files = sorted(d.glob("*.wav"))
    ok = True
    missing = set(manifest) - {p.stem for p in files}
    extra = {p.stem for p in files} - set(manifest)
    if missing or extra:
        print("manifest/file mismatch: missing", sorted(missing), "extra", sorted(extra))
        ok = False
    print(f"{'name':24s} {'dur':>6s} {'peak':>7s} {'rms':>7s} {'dc':>9s} {'cent':>6s}  tail/loop-seam")
    for name in manifest:
        p = d / f"{name}.wav"
        if not p.exists():
            continue
        m = analyse(p, manifest[name])
        seam = "" if m["loop"] else f"tail {m['tail_db']:.0f} dB"
        if m["loop"]:
            seam = (f"seam {m['seam_abs']:.5f} ({m['seam_x_rms']:.1f}x rms-step, {m['seam_x_max']:.2f}x max-step) | "
                    f"rms jump 25ms {m['seam_rms25']:.2f} dB (p{m['seam_pct25']:.0f}) | "
                    f"100ms {m['seam_rms100']:.2f} dB (p{m['seam_pct100']:.0f})")
        print(f"{name:24s} {m['dur']:6.3f} {m['peak_db']:7.2f} {m['rms_db']:7.2f} {m['dc']:+9.6f} {m['centroid']:6.0f}  {seam}"
              + ("   FAIL: " + "; ".join(m["fail"]) if m["fail"] else ""))
        ok &= not m["fail"]
    if args.hashes:
        for k, v in hashes(d).items():
            print(v, k)
    if args.determinism:
        ok &= determinism()
    if args.spectrograms:
        spectrograms(d, Path(args.spectrograms[0]), args.spectrograms[1:])
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
