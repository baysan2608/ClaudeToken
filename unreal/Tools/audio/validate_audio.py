#!/usr/bin/env python3
"""Validate the generated Fourfold sound set (unreal/SourceArt/Audio/{SFX,Ambience}) and render spectrogram sheets.

    python validate_audio.py                              # per-file checks + loudness table, exit 1 on any failure
    python validate_audio.py --loudness                   # per category K-weighted loudness summary (after manifest gain)
    python validate_audio.py --sheet OUTDIR swing_ kick_  # spectrogram sheet(s) of every sound whose name starts with a prefix
    python validate_audio.py --spectrograms OUTDIR a b c  # sheet of the named sounds
    python validate_audio.py --determinism                # render twice into temp dirs, compare SHA-256 of every WAV

Checks per file: format (mono 16-bit 48 kHz), duration, peak (-3 dBFS), clipping, DC, digital silence, one-shot ends at
zero / faded tail, loop seam (step across the seam, RMS continuity ranked against every other position), manifest
agreement (gain, loop, duration).  Run with a Python that has numpy + scipy (+ matplotlib for sheets), e.g.
/home/user/tools/bpyenv/bin/python.
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
UE_ROOT = HERE.parent.parent
AUDIO_ROOT = UE_ROOT / "SourceArt" / "Audio"
MANIFEST = UE_ROOT / "Content" / "Fourfold" / "Data" / "sfx_manifest.json"
SR = 48000
PEAK_TARGET = -3.0


def load(path: Path):
    with wave.open(str(path), "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    return x, (ch, sw, sr)


def db(v):
    return 20 * np.log10(max(float(v), 1e-12))


def find_wavs(root: Path) -> dict[str, Path]:
    out = {}
    for sub in ("SFX", "Ambience"):
        for p in sorted((root / sub).glob("*.wav")):
            out[p.stem] = p
    return out


# ------------------------------------------------------------------------------------------------ loudness (K-weighted)

_K1 = (np.array([1.53512485958697, -2.69169618940638, 1.19839281085285]), np.array([1.0, -1.69065929318241, 0.73248077421585]))
_K2 = (np.array([1.0, -2.0, 1.0]), np.array([1.0, -1.99004745483398, 0.99007225036621]))


def k_weight(xf: np.ndarray) -> np.ndarray:
    """ITU-R BS.1770 K-weighting (48 kHz coefficients)."""
    from scipy import signal
    return signal.lfilter(*_K2, signal.lfilter(*_K1, xf))


def loudest_db(xf: np.ndarray, win_s: float = 0.4) -> float:
    """Loudest window (K-weighted mean square, dB re full scale, like momentary LUFS without the -0.691 offset)."""
    k = k_weight(xf)
    w = int(win_s * SR)
    if len(k) < w:
        k = np.concatenate([k, np.zeros(w - len(k))])
    cs = np.concatenate([[0.0], np.cumsum(k ** 2)])
    starts = np.arange(0, len(k) - w + 1, max(w // 4, 1))
    ms = (cs[starts + w] - cs[starts]) / w
    return 10 * np.log10(max(float(ms.max()), 1e-12)) - 0.691


def loudest_100ms_db(xf: np.ndarray) -> float:
    return loudest_db(xf, 0.1)


# ------------------------------------------------------------------------------------------------ per-file checks


def analyse(path: Path, entry: dict | None, idx: dict | None):
    x, (ch, sw, sr) = load(path)
    n = len(x)
    xf = x / 32768.0
    out = {"name": path.stem, "n": n, "dur": n / SR, "fail": [], "warn": []}
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
    tot = max(float(np.sum(spec)), 1e-12)
    out["centroid"] = float(np.sum(freqs * spec) / tot)
    out["above150"] = float(np.sum(spec[freqs >= 150]) / tot)
    loop = bool((idx or {}).get("loop", entry and entry.get("loop")))
    out["loop"] = loop
    amb = (idx or {}).get("category") == "ambience" or path.parent.name == "Ambience"
    if abs(out["peak_db"] - PEAK_TARGET) > 0.25:
        out["fail"].append(f"peak {out['peak_db']:.2f} dBFS, expected about {PEAK_TARGET}")
    if out["clip"]:
        out["fail"].append(f"{out['clip']} clipped samples")
    if out["dc_rel"] > 0.01 or abs(out["dc"]) > 0.001:
        out["fail"].append(f"DC offset {out['dc']:.5f}")
    if out["rms_db"] < -60:
        out["fail"].append("practically silent")
    if out["above150"] < 0.15 and not amb:
        out["warn"].append(f"only {100 * out['above150']:.0f}% of the energy above 150 Hz (weak on phone speakers)")
    if loop:
        hi = 30.0 if amb else 3.0
        if not 1.0 <= out["dur"] <= hi:
            out["fail"].append(f"loop length outside 1-{hi:g} s")
        steps = np.abs(np.diff(x))
        seam = abs(x[0] - x[-1])
        out["seam_abs"] = seam / 32768.0
        out["seam_x_rms"] = seam / max(float(np.sqrt(np.mean(steps ** 2))), 1.0)
        out["seam_x_max"] = seam / max(float(steps.max()), 1.0)
        if out["seam_x_max"] > 1.25:
            out["fail"].append(f"seam step is {out['seam_x_max']:.2f}x the largest interior step")
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
        limit = float((idx or {}).get("max_dur", 1.5)) if idx else 1.5
        if out["dur"] > limit + 1e-6:
            out["fail"].append(f"one-shot longer than {limit} s")
        if out["dur"] < 0.1:
            out["fail"].append("one-shot shorter than 0.1 s")
        out["head"] = abs(xf[0])
        out["tail"] = abs(xf[-1])
        w = int(0.01 * SR)
        out["tail_db"] = db(np.sqrt(np.mean(xf[-w:] ** 2))) - out["peak_db"]
        if out["head"] > 0.002 or out["tail"] > 0.002:
            out["fail"].append("non-zero first/last sample (click risk)")
        if out["tail_db"] > -40:
            out["fail"].append(f"tail cut off ({out['tail_db']:.0f} dB re peak in last 10 ms)")
    if idx is not None and abs(idx["duration_s"] - out["dur"]) > 0.002:
        out["fail"].append("index duration mismatch (re-render or rebuild the manifest)")
    if entry is not None:
        if bool(entry.get("loop", False)) != loop:
            out["fail"].append("manifest loop flag disagrees with the index")
        if idx is not None and abs(float(entry.get("gain_db", 0.0)) - float(idx["suggested_volume_db"])) > 0.01:
            out["warn"].append("manifest gain_db differs from the generator's suggested volume (hand-tuned?)")
    return out


def sha(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def determinism():
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        for d in (a, b):
            subprocess.run([sys.executable, str(HERE / "render_audio.py"), "--out", d], check=True, stdout=subprocess.DEVNULL)
        ha = {p.name: sha(p) for p in sorted(Path(a).rglob("*.wav"))}
        hb = {p.name: sha(p) for p in sorted(Path(b).rglob("*.wav"))}
    same = ha == hb
    comb = hashlib.sha256("".join(f"{k}:{v}" for k, v in sorted(ha.items())).encode()).hexdigest()
    print(f"determinism: {len(ha)} files, run A == run B: {same}; combined sha256 {comb[:16]}")
    return same


# ------------------------------------------------------------------------------------------------ spectrograms


def sheet(wavs: dict[str, Path], names: list[str], outdir: Path, title: str, cols: int = 3, max_khz: float = 16.0) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy import signal
    outdir.mkdir(parents=True, exist_ok=True)
    rows = (len(names) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(5.2 * cols, 2.5 * rows), squeeze=False)
    for ax, name in zip(axes.ravel(), names):
        x, _ = load(wavs[name])
        xf = x / 32768.0
        nper = 1024 if len(xf) < SR * 4 else 2048
        f, t, S = signal.spectrogram(xf, SR, nperseg=nper, noverlap=nper * 7 // 8, window="hann")
        ax.pcolormesh(t, f / 1000, 10 * np.log10(S + 1e-12), vmin=-105, vmax=-35, shading="auto", cmap="magma")
        ax.set_title(f"{name}  ({len(xf) / SR:.2f}s)", fontsize=9)
        ax.set_ylim(0, max_khz)
        ax.tick_params(labelsize=7)
    for ax in axes.ravel()[len(names):]:
        ax.axis("off")
    fig.tight_layout()
    path = outdir / f"{title}.png"
    fig.savefig(path, dpi=70)
    plt.close(fig)
    print("spectrogram sheet:", path)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=AUDIO_ROOT)
    ap.add_argument("--manifest", type=Path, default=MANIFEST)
    ap.add_argument("--determinism", action="store_true")
    ap.add_argument("--loudness", action="store_true", help="per-category K-weighted loudness summary")
    ap.add_argument("--sheet", nargs="+", metavar=("OUTDIR", "PREFIX"), help="spectrogram sheets by name prefix")
    ap.add_argument("--spectrograms", nargs="+", metavar=("OUTDIR", "NAME"))
    ap.add_argument("--quiet", action="store_true", help="only print failures and the summary")
    args = ap.parse_args()
    wavs = find_wavs(args.root)
    index = json.loads((args.root / "sound_index.json").read_text()) if (args.root / "sound_index.json").exists() else {}
    manifest = {}
    if args.manifest.exists():
        manifest = json.loads(args.manifest.read_text()).get("sounds", {})
    if args.sheet:
        outdir = Path(args.sheet[0])
        for pre in args.sheet[1:]:
            names = [n for n in wavs if n.startswith(pre)]
            if names:
                sheet(wavs, names, outdir, "sheet_" + pre.strip("_"))
        return 0
    if args.spectrograms:
        sheet(wavs, args.spectrograms[1:], Path(args.spectrograms[0]), "spec_" + "_".join(args.spectrograms[1:4]))
        return 0
    ok = True
    missing = set(index) - set(wavs)
    extra = set(wavs) - set(index)
    if index and (missing or extra):
        print("index/file mismatch: missing", sorted(missing), "extra", sorted(extra))
        ok = False
    if manifest:
        mm = set(wavs) - set(manifest)
        if mm:
            print("sounds without a manifest entry:", sorted(mm))
            ok = False
        ghost = set(manifest) - set(wavs)
        if ghost:
            print("manifest entries without a wav:", sorted(ghost))
            ok = False
    rows = []
    nwarn = 0
    if not args.quiet:
        print(f"{'name':26s} {'dur':>6s} {'peak':>7s} {'rms':>7s} {'cent':>6s} {'>150':>5s}  tail/loop-seam")
    for name, p in wavs.items():
        m = analyse(p, manifest.get(name), index.get(name))
        rows.append((name, m))
        seam = ""
        if m["loop"]:
            seam = (f"seam {m['seam_abs']:.5f} ({m['seam_x_max']:.2f}x max-step) | rms jump 25ms {m['seam_rms25']:.2f} dB"
                    f" (p{m['seam_pct25']:.0f}) 100ms {m['seam_rms100']:.2f} dB (p{m['seam_pct100']:.0f})")
        else:
            seam = f"tail {m['tail_db']:.0f} dB"
        bad = m["fail"] or m["warn"]
        if not args.quiet or m["fail"]:
            print(f"{name:26s} {m['dur']:6.3f} {m['peak_db']:7.2f} {m['rms_db']:7.2f} {m['centroid']:6.0f} {100 * m['above150']:4.0f}%  {seam}"
                  + ("   FAIL: " + "; ".join(m["fail"]) if m["fail"] else "")
                  + ("   warn: " + "; ".join(m["warn"]) if m["warn"] and not m["fail"] else ""))
        nwarn += bool(m["warn"])
        ok &= not m["fail"]
    if args.loudness:
        cats = {}
        for name, m in rows:
            e = manifest.get(name) or {}
            idx = index.get(name, {})
            gain = float(e.get("gain_db", idx.get("suggested_volume_db", 0.0)))
            x, _ = load(wavs[name])
            lv = loudest_100ms_db(x / 32768.0) + gain
            key = f"{idx.get('bus', e.get('bus', '?'))}/{idx.get('category', e.get('category', '?'))}"
            cats.setdefault(key, []).append((lv, name))
        print("\nK-weighted loudest-100ms level after gain (dB re FS), by bus/category: min / median / max  (outliers > 6 dB from median)")
        for key, vals in sorted(cats.items()):
            v = np.array([a for a, _ in vals])
            med = float(np.median(v))
            out = [f"{b}({a:.0f})" for a, b in vals if abs(a - med) > 6.0]
            print(f"  {key:18s} n={len(v):3d}  {v.min():6.1f} / {med:6.1f} / {v.max():6.1f}   spread {v.max() - v.min():4.1f}"
                  + (f"   outliers: {', '.join(out)}" if out else ""))
    if args.determinism:
        ok &= determinism()
    print(f"\n{len(wavs)} files, {nwarn} with warnings. RESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
