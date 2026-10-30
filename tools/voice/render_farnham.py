"""Render Farnham's fruit-machine barks with Breeze-TTS-2. Run inside WSL (Breeze is Linux-only).

    powershell -File tools\\voice\\render_farnham.ps1 [--only win] [--force]

Reuses the Breeze wrapper from ../gaming-bot-test/voice/breeze.py. First it designs Farnham's voice from
farnham_barks.json ("voice" + "reference_text") into tools/voice/farnham_reference.wav; every bark is then
cloned from that reference so all clips sound like the same man. Delete the reference to design a new voice.

Clips land in assets/sfx/farnham/<category><NN>.wav as 22050 Hz mono 16-bit (vanilla's speech format),
silence-trimmed and peak-normalised. Existing clips are skipped unless --force.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT_RATE = 22050


def finish(src: Path, dst: Path) -> float:
    """Trim, normalise and resample a raw Breeze clip into the game's format. Returns its length in seconds."""
    audio, rate = sf.read(src, dtype="float32")
    loud = np.flatnonzero(np.abs(audio) > 0.02)
    if loud.size:
        pad = int(rate * 0.05)
        audio = audio[max(0, loud[0] - pad):loud[-1] + pad]
    peak = float(np.max(np.abs(audio))) or 1.0
    audio = audio * (0.7 / peak)
    audio = resample_poly(audio, OUT_RATE, rate).astype(np.float32)
    dst.parent.mkdir(parents=True, exist_ok=True)
    sf.write(dst, np.clip(audio, -1, 1), OUT_RATE, subtype="PCM_16")
    return len(audio) / OUT_RATE


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--breeze", type=Path, required=True, help="breeze-tts source checkout")
    ap.add_argument("--model", type=Path, required=True, help="downloaded Breeze-TTS-2 weights")
    ap.add_argument("--wrapper", type=Path, required=True, help="gaming-bot-test/voice (holds breeze.py)")
    ap.add_argument("--only", help="render just this category")
    ap.add_argument("--force", action="store_true", help="re-render clips that already exist")
    ap.add_argument("--cfg-scale", type=float, default=4.0)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    sys.path.insert(0, str(args.wrapper))
    import breeze  # noqa: E402

    manifest = json.loads((HERE / "farnham_barks.json").read_text(encoding="utf-8"))
    # The wrapper reads REFERENCE_TEXT at call time as the reference clip's transcript; ours says something else.
    breeze.REFERENCE_TEXT = manifest["reference_text"]
    tts = breeze.Breeze(args.breeze, args.model)

    reference = HERE / "farnham_reference.wav"
    if not reference.exists():
        took = tts.render(reference, manifest["reference_text"], instruction=manifest["voice"],
                          cfg_scale=args.cfg_scale, seed=args.seed)
        print(f"{took:5.1f}s  farnham_reference.wav", flush=True)

    raw = Path("/tmp/farnham_raw")
    out_dir = REPO / "assets" / "sfx" / "farnham"
    for category, lines in manifest["barks"].items():
        if args.only and category != args.only:
            continue
        delivery = manifest["delivery"].get(category)
        for i, text in enumerate(lines):
            out = out_dir / f"{category}{i + 1:02d}.wav"
            if out.exists() and not args.force:
                continue
            tmp = raw / out.name
            took = tts.render(tmp, text, instruction=delivery, reference=reference,
                              cfg_scale=args.cfg_scale, seed=args.seed + i)
            length = finish(tmp, out)
            print(f"{took:5.1f}s  {length:4.1f}s  {out.name}  {text}", flush=True)


if __name__ == "__main__":
    main()
