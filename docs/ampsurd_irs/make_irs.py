"""AMPSURD synthetic cab IR generator.

Builds minimum-phase impulse responses purely from parametric EQ models
(speaker + cabinet, microphone, mic position, voicing). No recorded IR is
used as input, so every output is an original, parameter-defined design.

Usage: python3 make_irs.py [out_dir]
"""
import sys, os, wave, zlib
import numpy as np
from scipy import signal

N_FFT = 16384
IR_LEN = 2048

# ---------- RBJ biquads -> magnitude in dB on a frequency grid ----------
def _biquad(kind, f0, fs, gain=0.0, q=0.707):
    A = 10 ** (gain / 40)
    w = 2 * np.pi * f0 / fs
    a = np.sin(w) / (2 * q)
    c = np.cos(w)
    if kind == "peak":
        b = [1 + a * A, -2 * c, 1 - a * A]; d = [1 + a / A, -2 * c, 1 - a / A]
    elif kind == "hp":
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; d = [1 + a, -2 * c, 1 - a]
    elif kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; d = [1 + a, -2 * c, 1 - a]
    elif kind in ("ls", "hs"):
        sq = 2 * np.sqrt(A) * a
        if kind == "ls":
            b = [A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq)]
            d = [(A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq]
        else:
            b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
            d = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    return np.array(b), np.array(d)


def response_db(stages, freqs, fs):
    db = np.zeros_like(freqs)
    for st in stages:
        kind, f0, gain, q = st
        b, d = _biquad(kind, f0, fs, gain, q)
        _, h = signal.freqz(b, d, worN=freqs, fs=fs)
        db += 20 * np.log10(np.maximum(np.abs(h), 1e-9))
    return db

# ---------- Models (own parametric designs) ----------
# (kind, freq Hz, gain dB, Q)
SPEAKERS = {
    # British, mid-forward, upper-mid bite (kept moderate)
    "V30": [("hp", 72, 0, 1.0), ("peak", 110, 2.0, 1.2), ("peak", 280, 1.5, 0.9),
            ("peak", 650, 1.0, 1.0), ("peak", 1400, 0.5, 2.0), ("peak", 2600, 3.0, 1.6),
            ("peak", 4200, 1.0, 3.0), ("peak", 5800, -5.0, 4.0), ("peak", 7000, 1.0, 4.0),
            ("lp", 8500, 0, 0.75)],
    # Tight lows, slightly leaner lower mids, focused 3 kHz
    "DV77": [("hp", 82, 0, 1.0), ("peak", 125, 2.0, 1.3), ("peak", 320, 1.0, 1.0),
             ("peak", 750, -0.5, 1.0), ("peak", 1800, 0.5, 2.0), ("peak", 3200, 3.0, 1.6),
             ("peak", 5000, 0.5, 3.0), ("peak", 6500, -4.0, 4.0),
             ("lp", 9000, 0, 0.75)],
    # Big, deep low end, broad warm mids
    "SWAMP": [("hp", 68, 0, 1.1), ("peak", 100, 3.0, 1.1), ("peak", 250, 2.0, 0.9),
              ("peak", 600, 0.5, 1.2), ("peak", 1200, 1.0, 1.8), ("peak", 2400, 2.5, 1.6),
              ("peak", 4500, 1.0, 2.5), ("peak", 6000, -4.0, 4.0),
              ("lp", 8500, 0, 0.75)],
}

MICS = {  # all ~1" from grille -> proximity effect
    "R121": [("ls", 160, 3.0, 0.7), ("peak", 300, 1.0, 1.0), ("hs", 5000, -4.0, 0.7), ("lp", 11000, 0, 0.7)],
    "SM57": [("hp", 60, 0, 0.7), ("ls", 180, 2.0, 0.7), ("peak", 5600, 2.5, 1.6), ("lp", 13000, 0, 0.7)],
    "MD421": [("ls", 150, 3.0, 0.7), ("peak", 4500, 2.0, 1.4), ("lp", 14000, 0, 0.7)],
}

# Cap/cone edge + 5 mm toward cone: a touch less top, a touch more body
POSITION = [("hs", 4000, -2.0, 0.7), ("peak", 500, 0.8, 0.8)]

# Target voicing: tight lows, full low-mids/mids, no nasal, smooth upper mids
VOICING = [("hp", 65, 0, 0.8), ("peak", 110, -2.0, 1.0),
           ("peak", 300, 1.5, 0.8), ("peak", 600, 1.5, 1.0),
           ("peak", 1000, -1.2, 1.6),
           ("peak", 3000, -1.0, 1.0), ("peak", 4500, -1.5, 1.5),
           ("lp", 11000, 0, 0.6), ("lp", 11000, 0, 0.6)]


def cone_ripple(freqs, seed):
    """Smooth, deterministic cone-breakup ripple above ~1.5 kHz (+-~1.5 dB)."""
    rng = np.random.default_rng(seed)
    lf = np.log2(np.maximum(freqs, 20))
    knots = np.linspace(np.log2(20), np.log2(24000), 60)
    vals = rng.normal(0, 1.0, knots.size)
    rip = np.interp(lf, knots, vals)
    weight = np.clip((lf - np.log2(1500)) / 2.0, 0, 1)
    return 1.5 * rip * weight / max(1e-9, np.abs(vals).max()) * 1.5


def min_phase_ir(mag_db, n_fft):
    mag = 10 ** (mag_db / 20)
    log_mag = np.log(np.maximum(mag, 1e-9))
    full = np.concatenate([log_mag, log_mag[-2:0:-1]])
    cep = np.fft.ifft(full).real
    win = np.zeros(n_fft); win[0] = 1; win[1:n_fft // 2] = 2; win[n_fft // 2] = 1
    return np.fft.ifft(np.exp(np.fft.fft(cep * win))).real


def write_wav24(path, x, fs):
    x = np.clip(x, -1, 1 - 2 ** -23)
    ints = np.round(x * (2 ** 23)).astype(np.int32)
    b = ints.astype("<i4").tobytes()
    data = b"".join(b[i:i + 3] for i in range(0, len(b), 4))
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(fs); w.writeframes(data)


CORRECTION_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "eq", "target_eq_v3.csv")


def correction_db(freqs):
    """Voicing correction tuned by ear in the AMPSURD EQ (v3)."""
    if not os.path.exists(CORRECTION_CSV):
        return np.zeros_like(freqs)
    c = np.loadtxt(CORRECTION_CSV, delimiter=",")
    return np.interp(np.log2(freqs), np.log2(c[:, 0]), c[:, 1])


POSITIONS_MM = list(range(25, 105, 5))  # distance from speaker centre (25 mm = cap/cone joint)
REF_MM = 25                              # v2 voicing corresponds to this spot


def position_stages(r):
    """Mic offset from dust-cap centre (mm), 12" speaker.
    Centre: brightest, dust-cap resonance; moving out onto the cone:
    progressive HF beaming loss, more low-mid body, a slowly moving
    interference notch."""
    cap = 2.5 * np.exp(-(r / 18.0) ** 2)
    return [("peak", 5000, cap, 2.0),
            ("hs", 3500, -0.05 * r, 0.7),
            ("lp", 16000 * 2 ** (-r / 45.0), 0, 0.6),
            ("peak", 300, 0.03 * r, 0.8),
            ("peak", 9000 * 2 ** (-r / 60.0), -3.0 * min(1.0, r / 30.0), 3.0)]


def position_delta_db(r, freqs, fs):
    return response_db(position_stages(r), freqs, fs) - response_db(position_stages(REF_MM), freqs, fs)


def build(spk, mic, fs, r=REF_MM):
    freqs = np.fft.rfftfreq(N_FFT, 1 / fs)
    freqs[0] = 1.0
    stages = SPEAKERS[spk] + MICS[mic] + POSITION + VOICING
    db = response_db(stages, freqs, fs)
    db += cone_ripple(freqs, zlib.crc32(f"{spk}-{mic}".encode()))
    db += position_delta_db(r, freqs, fs)
    db[freqs > 20000] -= 40
    ir = min_phase_ir(db, N_FFT)[:IR_LEN]
    fade = 256
    ir[-fade:] *= np.hanning(2 * fade)[fade:]
    return ir, freqs, db


NAMES = {"V30": "30", "DV77": "77", "SWAMP": "SWAMP", "R121": "121", "SM57": "57", "MD421": "421"}


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for fs in (48000, 44100):
        tag = "48k" if fs == 48000 else "44.1k"
        irs = {(s, m, r): build(s, m, fs, r)[0] for s in SPEAKERS for m in MICS for r in POSITIONS_MM}
        # loudness-match by energy, then common peak headroom
        scaled = {k: v / np.sqrt(np.sum(v ** 2)) for k, v in irs.items()}
        peak = max(np.max(np.abs(x)) for x in scaled.values())
        for (s, m, r), x in scaled.items():
            sub = os.path.join(out_dir, tag, f"{NAMES[s]}-{NAMES[m]}")
            os.makedirs(sub, exist_ok=True)
            write_wav24(os.path.join(sub, f"AMPSURD-412-{NAMES[s]}-{NAMES[m]}-P{r:03d}mm.wav"),
                        x / peak * 10 ** (-1 / 20), fs)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "AMPSURD_IRs")
