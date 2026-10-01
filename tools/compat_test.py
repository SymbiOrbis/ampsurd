#!/usr/bin/env python3
"""
Compatibility + correctness test.

For every .nam file given (default: all example models shipped with NeuralAmpModelerCore),
render the same input through
  (a) NeuralAmpModelerCore's official reference `render` tool, and
  (b) MONSTROSITY's engine (`monstrosity_render`, the code path the plugin uses),
then compare sample by sample.

Usage:
  python tools/compat_test.py --ref <path/to/NAM render> --ours <path/to/monstrosity_render>
                              --input <48k mono wav> [models...]
"""
import argparse, glob, os, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile


def read(path):
    sr, x = wavfile.read(path)
    return sr, x.astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--ours", required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--block", type=int, default=64)
    ap.add_argument("models", nargs="*")
    a = ap.parse_args()

    models = a.models or sorted(glob.glob(os.path.join(os.path.dirname(a.input), "..", "example_models", "*.nam")))
    tmp = tempfile.mkdtemp()
    fails = 0
    print(f"{'model':38s} {'result':8s} {'max|diff|':>11s} {'diff dB rel. RMS':>17s}")
    for m in models:
        name = os.path.basename(m)
        ref_out, our_out = os.path.join(tmp, "ref.wav"), os.path.join(tmp, "ours.wav")
        r1 = subprocess.run([a.ref, m, a.input, ref_out], capture_output=True, text=True)
        r2 = subprocess.run([a.ours, m, a.input, our_out, "--block", str(a.block)], capture_output=True, text=True)
        if r2.returncode != 0:
            print(f"{name:38s} {'LOADFAIL':8s}  {r2.stderr.strip()[-80:]}")
            fails += 1
            continue
        if r1.returncode != 0:
            print(f"{name:38s} {'NO-REF':8s}  reference tool failed: {r1.stderr.strip()[-60:]}")
            continue
        _, y_ref = read(ref_out)
        _, y_our = read(our_out)
        n = min(len(y_ref), len(y_our))
        d = y_our[:n] - y_ref[:n]
        rms_ref = np.sqrt(np.mean(y_ref[:n] ** 2)) + 1e-30
        rel_db = 20 * np.log10(np.sqrt(np.mean(d ** 2)) / rms_ref + 1e-30)
        ok = rel_db < -80.0  # difference at least 80 dB below the signal = numerically identical
        fails += 0 if ok else 1
        print(f"{name:38s} {'PASS' if ok else 'FAIL':8s} {np.max(np.abs(d)):11.2e} {rel_db:17.1f}")
    print("\nALL PASS" if fails == 0 else f"\n{fails} FAILURE(S)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
