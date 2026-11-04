"""Render a cast's voice lines with Breeze-TTS-2. Run inside WSL (Breeze is Linux-only).

    powershell -File tools\\voice\\render_voice.ps1 <cast> [--only win] [--force]

Each cast has a manifest, tools/voice/<cast>_barks.json. The voice is designed once from its "voice" and
"reference_text" into tools/voice/<cast>_reference.wav, and every line is cloned from that reference so they
all sound like the same speaker. Delete the reference to recast.

Clips land in assets/sfx/<cast>/<category><NN>.wav as 22050 Hz mono 16-bit (vanilla's speech format),
silence-trimmed and peak-normalised. An optional "effect" block in the manifest pitches the voice down and adds
a cave reverb; "effect_by_category" overrides it per category. Existing clips are skipped unless --force.

The "rumble" cast needs no Breeze: `--cast rumble` synthesises the collapse's stone-breaking stingers instead.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, fftconvolve, resample_poly, sosfilt

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT_RATE = 22050


def apply_effect(audio: np.ndarray, rate: int, effect: dict, seed: int) -> np.ndarray:
    """Pitch down by resampling (slows the line too, which suits these voices) and add a synthetic cave reverb."""
    semitones = effect.get("pitch_semitones", 0.0)
    if semitones:
        ratio = 2.0 ** (semitones / 12.0)
        # Playing back at `ratio` speed == resampling to rate / ratio and keeping the rate label.
        audio = resample_poly(audio, 1000, int(round(1000 * ratio)))
    mix = effect.get("reverb_mix", 0.0)
    seconds = effect.get("reverb_seconds", 0.0)
    if mix and seconds:
        rng = np.random.default_rng(seed)
        n = int(rate * seconds)
        tail = rng.standard_normal(n) * np.exp(-6.0 * np.linspace(0.0, 1.0, n))
        # Stone swallows the highs: a low-passed tail reads as a cavern rather than a tiled room.
        tail = sosfilt(butter(2, 2500, "low", fs=rate, output="sos"), tail)
        tail /= np.sqrt(np.sum(tail**2)) or 1.0
        wet = fftconvolve(audio, tail)
        dry = np.concatenate([audio, np.zeros(len(wet) - len(audio))])
        audio = dry * (1.0 - mix) + wet * mix
    return audio


def finish(src: Path, dst: Path, effect: dict | None, seed: int) -> float:
    """Trim, normalise and resample a raw Breeze clip into the game's format. Returns its length in seconds."""
    audio, rate = sf.read(src, dtype="float32")
    loud = np.flatnonzero(np.abs(audio) > 0.02)
    if loud.size:
        pad = int(rate * 0.05)
        audio = audio[max(0, loud[0] - pad):loud[-1] + pad]
    if effect:
        audio = apply_effect(audio, rate, effect, seed)
    peak = float(np.max(np.abs(audio))) or 1.0
    audio = audio * (0.7 / peak)
    audio = resample_poly(audio, OUT_RATE, rate).astype(np.float32)
    dst.parent.mkdir(parents=True, exist_ok=True)
    sf.write(dst, np.clip(audio, -1, 1), OUT_RATE, subtype="PCM_16")
    return len(audio) / OUT_RATE


def render_rumble(force: bool) -> None:
    """Stone-breaking stingers: filtered brown noise for the groan, band-passed bursts for falling rock."""
    out_dir = REPO / "assets" / "sfx" / "rumble"
    out_dir.mkdir(parents=True, exist_ok=True)
    # (seconds, peak level, cracks) from a distant groan up to the roof coming in.
    for index, (seconds, level, cracks) in enumerate([(3.0, 0.45, 4), (3.5, 0.55, 7), (3.0, 0.65, 11), (2.5, 0.8, 16)]):
        out = out_dir / f"rumble{index + 1:02d}.wav"
        if out.exists() and not force:
            continue
        rng = np.random.default_rng(100 + index)
        n = int(OUT_RATE * seconds)
        low = np.cumsum(rng.standard_normal(n))
        low -= np.linspace(low[0], low[-1], n)
        low = sosfilt(butter(4, [25, 160], "band", fs=OUT_RATE, output="sos"), low)
        low /= np.max(np.abs(low)) or 1.0
        # Slow surges, so it does not read as a steady hum.
        t = np.linspace(0.0, seconds, n)
        low *= 0.6 + 0.4 * np.sin(2 * np.pi * rng.uniform(0.7, 1.4) * t + rng.uniform(0, 6.28))

        debris = np.zeros(n)
        for _ in range(cracks):
            length = int(OUT_RATE * rng.uniform(0.04, 0.22))
            start = int(rng.integers(0, max(1, n - length)))
            burst = rng.standard_normal(length) * np.exp(-np.linspace(0, 7, length)) * rng.uniform(0.3, 1.0)
            debris[start:start + length] += burst
        debris = sosfilt(butter(2, [180, 2600], "band", fs=OUT_RATE, output="sos"), debris)
        debris /= np.max(np.abs(debris)) or 1.0

        audio = low + 0.45 * debris
        fade = int(OUT_RATE * 0.25)
        envelope = np.ones(n)
        envelope[:fade] = np.linspace(0, 1, fade)
        envelope[-2 * fade:] = np.linspace(1, 0, 2 * fade)
        audio = audio * envelope
        audio = audio * (level / (np.max(np.abs(audio)) or 1.0))
        sf.write(out, audio.astype(np.float32), OUT_RATE, subtype="PCM_16")
        print(f"{seconds:4.1f}s  {out.name}", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cast", required=True, help="manifest name: <cast>_barks.json, or 'rumble'")
    ap.add_argument("--breeze", type=Path, help="breeze-tts source checkout")
    ap.add_argument("--model", type=Path, help="downloaded Breeze-TTS-2 weights")
    ap.add_argument("--wrapper", type=Path, help="gaming-bot-test/voice (holds breeze.py)")
    ap.add_argument("--only", help="render just this category")
    ap.add_argument("--force", action="store_true", help="re-render clips that already exist")
    ap.add_argument("--cfg-scale", type=float, default=4.0)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    if args.cast == "rumble":
        render_rumble(args.force)
        return

    sys.path.insert(0, str(args.wrapper))
    import breeze  # noqa: E402

    manifest = json.loads((HERE / f"{args.cast}_barks.json").read_text(encoding="utf-8"))
    # The wrapper reads REFERENCE_TEXT at call time as the reference clip's transcript; ours says something else.
    breeze.REFERENCE_TEXT = manifest["reference_text"]
    tts = breeze.Breeze(args.breeze, args.model)

    reference = HERE / f"{args.cast}_reference.wav"
    if not reference.exists():
        took = tts.render(reference, manifest["reference_text"], instruction=manifest["voice"],
                          cfg_scale=args.cfg_scale, seed=args.seed)
        print(f"{took:5.1f}s  {reference.name}", flush=True)

    raw = Path(f"/tmp/{args.cast}_raw")
    out_dir = REPO / "assets" / "sfx" / args.cast
    effect = manifest.get("effect")
    for category, lines in manifest["barks"].items():
        if args.only and category != args.only:
            continue
        delivery = manifest["delivery"].get(category)
        # Urgent lines have to survive a fight: they can take a lighter effect than the rest.
        category_effect = manifest.get("effect_by_category", {}).get(category, effect)
        for i, text in enumerate(lines):
            out = out_dir / f"{category}{i + 1:02d}.wav"
            if out.exists() and not args.force:
                continue
            tmp = raw / out.name
            took = tts.render(tmp, text, instruction=delivery, reference=reference,
                              cfg_scale=args.cfg_scale, seed=args.seed + i)
            length = finish(tmp, out, category_effect, args.seed + i)
            print(f"{took:5.1f}s  {length:4.1f}s  {out.name}  {text}", flush=True)


if __name__ == "__main__":
    main()
