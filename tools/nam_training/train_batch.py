"""
AMPSURD - batch NAM training on a rented GPU (or any PC with the NAM trainer installed).

Does exactly what the NAM trainer window (neural-amp-modeler 0.13, "nam") does for a batch, without the window:
the same checks, the same automatic latency, A2, the same export (.nam with loudness / gain / training metadata).

Usage (one line):
    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs 400

  --input       the sweep / input file you reamped with (NAM standard v3)
  --recordings  a folder with the amp recordings (*.wav, 48 kHz, same length as the input)
  --dest        where the finished .nam files go (created if missing)
  --epochs      training passes (400 = like the earlier TONE3000 captures)
  --part K --parts N   (optional) train only every N-th recording, starting with the K-th: start the same command
                in N terminals with --part 1, 2 ... N (same --parts) to train N recordings at the same time

Faster: the model is small, so one training barely uses the GPU. E.g. three terminals:
    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs 400 --part 1 --parts 3
    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs 400 --part 2 --parts 3
    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs 400 --part 3 --parts 3

Each .nam gets the recording's name (CH1_CLEAN_G08_gain_0800.wav -> CH1_CLEAN_G08_gain_0800.nam).
Silent recordings (e.g. 7:00 with GAIN fully down) are skipped. A recording that fails the trainer's checks is
skipped and listed at the end; the others still train. Already finished .nam files are not trained again, so the
script can simply be started again after an interruption.
A summary (name, accuracy ESR, time) is written to <dest>/summary.txt (summary_partKofN.txt with --parts).
"""

import os

os.environ.setdefault("MPLBACKEND", "Agg")  # no plot windows: a failed check must not stop the batch waiting

import argparse
import re
import sys
import time
import wave
from pathlib import Path

import numpy as np
import torch

from nam.models.metadata import UserMetadata
from nam.train import core
from nam.train import metadata as train_metadata


def is_silent(path: Path) -> bool:
    """True when the recording is all (almost) zero - e.g. GAIN fully down."""
    with wave.open(str(path), "rb") as w:
        width = w.getsampwidth()
        frames = w.readframes(w.getnframes())
    if width == 3:  # 24-bit: take the top byte of each sample
        a = np.frombuffer(frames, dtype=np.uint8)[2::3].astype(np.int8)
        return int(np.max(np.abs(a.astype(np.int16)))) == 0
    dtype = {2: np.int16, 4: np.int32}.get(width)
    if dtype is None:
        return False  # 32-bit float or other: let the trainer judge it
    a = np.frombuffer(frames, dtype=dtype)
    return a.size == 0 or int(np.max(np.abs(a.astype(np.int64)))) < 8


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--recordings", required=True)
    ap.add_argument("--dest", required=True)
    ap.add_argument("--epochs", type=int, default=400)
    ap.add_argument("--part", type=int, default=1)
    ap.add_argument("--parts", type=int, default=1)
    args = ap.parse_args()
    if not (1 <= args.part <= args.parts):
        print("--part must be between 1 and --parts")
        return 1

    gpu = torch.cuda.is_available()
    print(f"GPU: {torch.cuda.get_device_name(0) if gpu else 'NONE - this will be slow'}")
    batch_size = 16 if gpu else 1  # what the NAM trainer window uses

    dest = Path(args.dest)
    dest.mkdir(parents=True, exist_ok=True)
    files = sorted(Path(args.recordings).glob("*.wav"))
    if not files:
        print("No .wav files found in", args.recordings)
        return 1
    files = files[args.part - 1 :: args.parts]  # this terminal's share
    tag = f"_part{args.part}of{args.parts}" if args.parts > 1 else ""
    print(f"part {args.part} of {args.parts}: {len(files)} recordings")

    done, skipped, failed = [], [], []
    for i, f in enumerate(files, 1):
        name = re.sub(r"\.wav$", "", f.name, flags=re.IGNORECASE)
        if (dest / f"{name}.nam").exists():
            print(f"[{i}/{len(files)}] {name}: already trained - skipped")
            continue
        if is_silent(f):
            print(f"[{i}/{len(files)}] {name}: silent recording - skipped")
            skipped.append(name)
            continue
        print(f"\n[{i}/{len(files)}] training {name} ({args.epochs} epochs) ...")
        t0 = time.time()
        try:
            out = core.train(
                args.input,
                str(f),
                str(dest / f"_work{tag}"),
                epochs=args.epochs,
                latency=None,
                silent=True,
                save_plot=False,
                modelname=name,
                ignore_checks=False,
                local=False,
                threshold_esr=None,
                user_metadata=UserMetadata(),
                batch_size=batch_size,
                seed=0,
            )
        except Exception as e:  # noqa: BLE001 - report and go on with the next recording
            print(f"  FAILED: {e}")
            failed.append(f"{name}: {e}")
            continue
        if out is None or out.model is None:
            print("  FAILED: the trainer's checks did not pass (see above)")
            failed.append(f"{name}: the trainer's checks did not pass")
            continue
        out.model.net.export(
            dest,
            basename=name,
            user_metadata=UserMetadata(),
            other_metadata={train_metadata.TRAINING_KEY: out.metadata.model_dump()},
        )
        esr = out.metadata.validation_esr
        minutes = (time.time() - t0) / 60.0
        print(f"  done in {minutes:.1f} min, ESR {esr}")
        done.append((name, esr, minutes))

    lines = ["AMPSURD batch training summary", f"epochs {args.epochs}", ""]
    for name, esr, minutes in done:
        lines.append(f"OK      {name}  ESR {esr}  ({minutes:.1f} min)")
    for name in skipped:
        lines.append(f"SKIPPED {name}  (silent)")
    for why in failed:
        lines.append(f"FAILED  {why}")
    (dest / f"summary{tag}.txt").write_text("\n".join(lines) + "\n")
    print("\n" + "\n".join(lines))
    return 0 if not failed else 2


if __name__ == "__main__":
    sys.exit(main())
