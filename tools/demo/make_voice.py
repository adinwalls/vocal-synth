#!/usr/bin/env python3
"""Build a stand-in "sung" voice for the demo video when there is no real singer.

Each word is spoken by the Flite text-to-speech voice built into ffmpeg, then
the WORLD vocoder (pyworld) holds it on a note of a melody. The pitch is left
deliberately imperfect (a few cents out, scoops, vibrato) so the plugin has
something to tune.

    pip install numpy pyworld soundfile
    python3 tools/demo/make_voice.py out.wav
"""
import subprocess, sys, tempfile, os
import numpy as np, pyworld as pw, soundfile as sf

FS = 48000
FRAME_MS = 5.0
# (word, MIDI note, seconds held, cents out of tune, rest after in seconds)
PHRASE = [
    ("hold", 57, 0.75, -28, 0.02),
    ("me", 59, 0.45, 22, 0.02),
    ("now", 61, 1.05, -35, 0.40),
    ("till", 61, 0.40, 18, 0.0),
    ("the", 59, 0.32, -15, 0.0),
    ("day", 57, 0.62, 30, 0.02),
    ("light", 59, 0.55, -24, 0.03),
    ("comes", 57, 1.35, -32, 0.55),
]

def speak(word, voice="slt"):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "w.wav")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", f"flite=text='{word}':voice={voice}", "-ar", str(FS), "-ac", "1", p], check=True)
        x, _ = sf.read(p)
    return x.astype(np.float64)

def sing(word, midi, hold, cents, rng):
    x = speak(word)
    f0, t = pw.harvest(x, FS, f0_floor=90, f0_ceil=450, frame_period=FRAME_MS)
    sp = pw.cheaptrick(x, f0, t, FS)
    ap = pw.d4c(x, f0, t, FS)
    # trim leading/trailing silence
    env = np.sqrt(sp.sum(axis=1))
    on = np.where(env > env.max() * 0.03)[0]
    a, b = max(0, on[0] - 4), min(len(f0), on[-1] + 8)
    f0, sp, ap = f0[a:b], sp[a:b], ap[a:b]
    # the longest voiced run holds the vowel: stretch it to fill the note ...
    v = f0 > 0
    best, start, run = (0, 0), 0, 0
    for i, on_ in enumerate(np.append(v, False)):
        if on_:
            if run == 0: start = i
            run += 1
        else:
            if run > best[1] - best[0]: best = (start, start + run)
            run = 0
    s, e = best
    n = len(f0)
    want = int(hold * 1000 / FRAME_MS)
    # ... around its loudest, brightest frames, so the vowel is held and not an l, m or n
    freqs = np.arange(sp.shape[1]) * FS / 2 / (sp.shape[1] - 1)
    band = (freqs > 600) & (freqs < 4000)
    bright = np.convolve(sp[:, band].sum(axis=1), np.ones(5) / 5, mode="same")
    peak = s + int(np.argmax(bright[s:e]))
    mid_a, mid_b = max(s + 2, peak - 5), min(e - 2, peak + 6)
    extra = max(want - n, -(mid_b - mid_a) + 4)
    mid = np.linspace(mid_a, mid_b - 1, (mid_b - mid_a) + extra)
    idx = np.concatenate([np.arange(0, mid_a), np.round(mid).astype(int), np.arange(mid_b, n)])
    f0, sp, ap = f0[idx], sp[idx], ap[idx]
    # voiced everywhere inside the vowel, then draw the sung pitch
    voiced = f0 > 0
    m = len(f0)
    tt = np.arange(m) * FRAME_MS / 1000
    vib_depth = 22 * np.clip((tt - 0.28) / 0.3, 0, 1)             # vibrato grows on held notes
    c = cents + vib_depth * np.sin(2 * np.pi * 5.4 * tt + rng.uniform(0, 6.28))
    c += -70 * np.exp(-tt / 0.06)                                   # scoop up into the note
    c += 6 * np.sin(2 * np.pi * 0.9 * tt + rng.uniform(0, 6.28))   # slow drift
    hz = 440.0 * 2 ** ((midi - 69) / 12) * 2 ** (c / 1200)
    f0 = np.where(voiced, hz, 0.0)
    y = pw.synthesize(f0, sp, ap, FS, FRAME_MS)
    fade = int(0.008 * FS)
    y[:fade] *= np.linspace(0, 1, fade); y[-fade:] *= np.linspace(1, 0, fade)
    return y

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo_voice.wav"
    rng = np.random.default_rng(7)
    parts = []
    for word, midi, hold, cents, rest in PHRASE:
        parts.append(sing(word, midi, hold, cents, rng))
        parts.append(np.zeros(int(rest * FS)))
    y = np.concatenate(parts)
    y *= 0.5 / np.abs(y).max()
    sf.write(out, y.astype(np.float32), FS, subtype="FLOAT")
    print(f"wrote {out} ({len(y) / FS:.2f} s)")

main()
