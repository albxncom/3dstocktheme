"""The measurement table of interface sounds (length, peak, level, partials over time, shares, level through the
imitation of the console's speakers), each against two references.

  python3 -I measure.py BACK.wav DROP.wav WAV [WAV ...]

The references are the tracked uikit/sound/back.wav and drop.wav, two sounds heard on the console.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp import RATE, read_wav, fft, shares, highpass, level

FC, ORDER = 600, 6
FRAME, HOP, NFFT = 640, 320, 4096


def peaks(frame, k=3):
    """Strongest k spectral peaks of one Hann-windowed, zero-padded frame: (Hz, dB re the frame's strongest)."""
    n = len(frame)
    w = [frame[i] * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i in range(n)]
    X = fft([complex(v) for v in w] + [0j] * (NFFT - n))
    m = [abs(X[i]) + 1e-9 for i in range(NFFT // 2)]
    loc = [i for i in range(1, NFFT // 2 - 1) if m[i] > m[i - 1] and m[i] >= m[i + 1]]
    loc.sort(key=lambda i: -m[i])
    out = []
    for i in loc:
        a, b, c = (math.log(m[j]) for j in (i - 1, i, i + 1))
        d = 0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c else 0.0
        f = (i + d) * RATE / NFFT
        if all(abs(f - g) > 40 for g, _ in out):
            out.append((f, 20 * math.log10(m[i])))
        if len(out) == k: break
    top = out[0][1]
    return [(round(f), round(v - top, 1)) for f, v in out]


def table(path, ref_after, drop_after):
    r, x = read_wav(path)
    assert r == RATE, (path, r)
    pk = max(abs(v) for v in x)
    s, cen = shares(x)
    after = level(highpass(x, FC, ORDER))
    print('##', os.path.basename(path), os.path.getsize(path), 'bytes')
    print('  samples %d, %.1f ms, peak %d (%.2f dBFS), level %.2f dBFS, clipped %d' % (
        len(x), len(x) / RATE * 1000, pk, 20 * math.log10(pk / 32768), level(x), sum(1 for v in x if v in (32767, -32768))))
    print('  above 300/500/800 Hz: %.1f / %.1f / %.1f %%, centroid %d Hz' % (100 * s[0], 100 * s[1], 100 * s[2], cen))
    print('  through the filter: %.2f dBFS; vs back.wav %+.2f dB; vs drop %+.2f dB' % (after, after - ref_after, after - drop_after))
    for i in range(0, len(x) - FRAME + 1, HOP * 2):
        fr = x[i:i + FRAME]
        rms = math.sqrt(sum(v * v for v in fr) / FRAME)
        db = 20 * math.log10(max(rms, 1e-9) / 32768)
        if db < -60: continue
        print('  t %5.1f ms  rms %6.1f dBFS  partials %s' % ((i + FRAME / 2) / RATE * 1000, db, peaks(fr)))


ref_after = level(highpass(read_wav(sys.argv[1])[1], FC, ORDER))
drop_after = level(highpass(read_wav(sys.argv[2])[1], FC, ORDER))
print('back.wav through the filter %.2f dBFS, drop %.2f dBFS' % (ref_after, drop_after))
for p in sys.argv[3:]:
    table(p, ref_after, drop_after)
