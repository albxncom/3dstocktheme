"""Makes popup.wav and error.wav louder for the console's speakers: the same sound with its partials moved up by a
number of semitones under its own envelope (length kept, nothing resampled), then one gain to a level of the loudest 50 ms.

popup.wav is one gliding tone: its analytic signal z = a e^(i phi) is multiplied by e^(i (r - 1) theta), theta the integral
of its measured, smoothed instantaneous frequency, so every moment's frequency f becomes r f under the same envelope a.
error.wav is two harmonic parts in a row (a buzz at about 50.6 Hz, then a tone at about 219 Hz): the tone's harmonics (from the
crossfade on) and everything else (cut into bands round the buzz's harmonics, raised-cosine crossovers that sum to one) move
up by their own k f0 (r - 1), rounded to a bin.

  python3 -I make_popup_error.py build popup|error IN.wav OUT.wav SEMITONES LEVEL_DB [BACK.wav]
  python3 -I make_popup_error.py search POPUP.wav ERROR.wav [BACK.wav]

IN.wav, POPUP.wav and ERROR.wav are the pack files decoded to 16-bit mono at 32000 Hz (README.md); each is converted
first as every interface sound is (-20 dBFS). BACK.wav, the reference the result is measured against through the
speakers' imitation, defaults to the tracked uikit/sound/back.wav. `search` lists 0 to 24 semitones at -14 dBFS for both;
r = 1 gives the converted input back (checked by `search`).
"""
import cmath, math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dsp import RATE, read_wav, write_wav, convert, fft, highpass, level, shares

BACK = os.path.join(HERE, '..', '..', 'uikit', 'sound', 'back.wav')

FC, ORDER = 600, 6
SPLIT = (78.0, 92.0)  # ms: error.wav's crossfade from the buzz to the tone (measured: the buzz's 50.6 Hz periodicity ends by 85 ms, the tone's 219 Hz is clear from 95 ms)


def ifft(X):
    n = len(X)
    return [v.conjugate() / n for v in fft([v.conjugate() for v in X])]


def padlen(n):
    m = 1
    while m < n + 8192: m *= 2
    return m


def analytic_spectrum(x, n):
    X = fft([complex(v) for v in x] + [0j] * (n - len(x)))
    return [X[0]] + [2 * X[j] for j in range(1, n // 2)] + [X[n // 2]] + [0j] * (n // 2 - 1)


def harmonic_f0(x, lo, hi, step=0.02, fmax=2000.0):
    """f0 whose harmonics up to fmax carry the most spectral magnitude (interpolated bins)."""
    n = padlen(len(x))
    X = fft([complex(v) for v in x] + [0j] * (n - len(x)))
    m = [abs(X[j]) for j in range(n // 2 + 1)]
    df = RATE / n

    def at(f):
        p = f / df; j = int(p); t = p - j
        return m[j] * (1 - t) + m[j + 1] * t
    best = None
    f = lo
    while f <= hi:
        s = sum(at(k * f) for k in range(1, int(fmax / f) + 1)) / int(fmax / f)
        if best is None or s > best[0]: best = (s, f)
        f += step
    return best[1]


def shift_comb(Z, n, f0, r, Y):
    """Add to Y every harmonic band k of the analytic spectrum Z (crossovers at (k +- 0.5) f0, raised cosine over f0 / 2)
    moved up by k f0 (r - 1), rounded to a bin."""
    df = RATE / n
    for j in range(n // 2 + 1):
        if Z[j] == 0: continue
        u = j * df / f0
        k = int(math.floor(u))
        for kk in (k, k + 1):
            d = abs(u - kk)
            w = 1.0 if d <= 0.25 else (math.cos(math.pi / 2 * (d - 0.25) / 0.5) ** 2 if d < 0.75 else 0.0)
            if w == 0: continue
            t = j + round(kk * f0 * (r - 1) / df)
            if 0 <= t <= n // 2: Y[t] += w * Z[j]


def tone_weight(u):
    """Share of a bin at u = f / f0 that belongs to the tone's nearest harmonic k >= 1: 1 within 1/8 f0, none from 3/8 f0."""
    k = max(1, round(u))
    d = abs(u - k)
    return k, 1.0 if d <= 0.125 else (math.cos(math.pi / 2 * (d - 0.125) / 0.25) ** 2 if d < 0.375 else 0.0)


def inst_freq(z, win=160):
    """Instantaneous frequency of the analytic signal z, smoothed by an |z|^2-weighted moving average over win samples."""
    raw = [cmath.phase(z[i + 1] * z[i].conjugate()) * RATE / (2 * math.pi) for i in range(len(z) - 1)]
    wt = [abs(z[i]) ** 2 for i in range(len(z) - 1)]
    sf, sw = [0.0], [0.0]
    for f, w in zip(raw, wt): sf.append(sf[-1] + f * w); sw.append(sw[-1] + w)
    h = win // 2
    out = []
    for i in range(len(raw)):
        a, b = max(0, i - h), min(len(raw), i + h + 1)
        out.append((sf[b] - sf[a]) / (sw[b] - sw[a]) if sw[b] - sw[a] > 0 else (out[-1] if out else 0.0))
    out += out[-1:]
    # Where the tone is under -60 dBFS (its first and last samples) the phase says nothing: hold the nearest good value.
    good = [i for i in range(len(z)) if abs(z[i]) > 32768 * 1e-3]
    return [out[min(max(i, good[0]), good[-1])] for i in range(len(z))]


def shift_glide(x, r):
    n = padlen(len(x))
    z = ifft(analytic_spectrum(x, n))[:len(x)]
    f = inst_freq(z)
    th, out = 0.0, []
    for i in range(len(x)):
        out.append((z[i] * cmath.exp(1j * (r - 1) * th)).real)
        th += 2 * math.pi * f[i] / RATE
    return out, f


def shift_error(x, r):
    """The buzz's comb takes everything but the tone's harmonics (also the buzz's own tail under the tone);
    the tone's harmonics, from the crossfade on, move by their own f0."""
    a, b = (int(v * RATE / 1000) for v in SPLIT)
    wb = [0.0 if i < a else 1.0 if i >= b else math.sin(math.pi / 2 * (i - a) / (b - a)) ** 2 for i in range(len(x))]
    fa = harmonic_f0(x[:a], 45, 56)
    fb = harmonic_f0(x[b:], 210, 230)
    n = padlen(len(x))
    df = RATE / n
    za = analytic_spectrum([v * (1 - w) for v, w in zip(x, wb)], n)
    zb = analytic_spectrum([v * w for v, w in zip(x, wb)], n)
    Y, rest = [0j] * n, list(za)
    for j in range(n // 2 + 1):
        k, w = tone_weight(j * df / fb)
        rest[j] += (1 - w) * zb[j]
        t = j + round(k * fb * (r - 1) / df)
        if w and 0 <= t <= n // 2: Y[t] += w * zb[j]
    shift_comb(rest, n, fa, r, Y)
    return [v.real for v in ifft(Y)[:len(x)]], (fa, fb)


def to_level(y, level_db):
    g = 10 ** ((level_db - level(y)) / 20)
    out = [math.floor(v * g + 0.5) for v in y]
    return [max(-32768, min(32767, v)) for v in out], sum(1 for v in out if v > 32767 or v < -32768)


def make(which, x, semis, level_db):
    r = 2 ** (semis / 12)
    if which == 'popup': y, info = shift_glide(x, r)
    else: y, info = shift_error(x, r)
    out, clipped = to_level(y, level_db)
    return out, clipped, info


def describe(y, ref_after):
    pk = max(abs(v) for v in y)
    s, cen = shares(y)
    after = level(highpass(y, FC, ORDER))
    return 'n %d (%.1f ms) peak %.2f dBFS level %.2f after %.2f (%+.2f vs back) above300/500/800 %.1f/%.1f/%.1f' % (
        len(y), len(y) / RATE * 1000, 20 * math.log10(pk / 32768), level(y), after, after - ref_after, 100 * s[0], 100 * s[1], 100 * s[2])


if sys.argv[1] == 'search':
    popup, error = (convert(read_wav(p)[1], -20.0) for p in sys.argv[2:4])
    back = read_wav(sys.argv[4] if len(sys.argv) > 4 else BACK)[1]
    ref_after = level(highpass(back, FC, ORDER))
    for which, x in (('popup', popup), ('error', error)):
        y0 = make(which, x, 0, level(x))[0]
        print(which, 'identity at r = 1: max |diff|', max(abs(p - q) for p, q in zip(x, y0)), 'info', make(which, x, 0, -20)[2] if which == 'error' else '')
        for semis in range(0, 25):
            y, clipped, info = make(which, x, semis, -14.0)
            print('  %s %2d st at -14: %s clipped %d' % (which, semis, describe(y, ref_after), clipped))
elif sys.argv[1] == 'build':
    which, src, dst, semis, lv = sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5]), float(sys.argv[6])
    x = convert(read_wav(src)[1], -20.0)
    y, clipped, info = make(which, x, semis, lv)
    assert clipped == 0, clipped
    write_wav(dst, y)
    print(which, 'semitones', semis, 'ratio %.4f' % 2 ** (semis / 12), 'info', info if which == 'error' else 'f track %.0f..%.0f Hz' % (min(info[160:-160]), max(info[160:-160])))
    print(describe(y, level(highpass(read_wav(sys.argv[7] if len(sys.argv) > 7 else BACK)[1], FC, ORDER))))
