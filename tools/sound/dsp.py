"""Signal helpers for the sound generators (standard library only): WAV in and out, the conversion of a pack file,
the level measure, energy shares, and the imitation of the console's speakers (a high-pass).
"""
import array, cmath, math, sys, wave

RATE = 32000


def read_wav(p):
    with wave.open(p, 'rb') as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, p
        r = w.getframerate()
        a = array.array('h')
        a.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder == 'big': a.byteswap()
    return r, list(a)


def write_wav(p, samples, rate=RATE):
    a = array.array('h', samples)
    if sys.byteorder == 'big': a.byteswap()
    with wave.open(p, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(a.tobytes())


def convert(s, level_db=-20.0):
    """The conversion of every interface sound (CREDITS.md): the silence cut (first sample over -45 dBFS minus 32,
    last over -50 dBFS plus 255), the loudest 1600-sample (50 ms) window to level_db RMS, the last 160 samples faded by
    (159 - j) / 160, rounded (half to even, as Python's round) and clamped to int16."""
    ts, te = 32768 * 10 ** (-45 / 20), 32768 * 10 ** (-50 / 20)
    first = next(i for i, v in enumerate(s) if abs(v) > ts)
    last = max(i for i, v in enumerate(s) if abs(v) > te)
    x = [float(v) for v in s[max(0, first - 32):min(len(s), last + 256)]]
    w = min(1600, len(x)); acc = sum(v * v for v in x[:w]); best = acc
    for i in range(w, len(x)):
        acc += x[i] * x[i] - x[i - w] * x[i - w]; best = max(best, acc)
    g = 32768 * 10 ** (level_db / 20) / math.sqrt(best / w)
    n = len(x)
    for j in range(max(0, n - 160), n):
        x[j] *= (159 - (j - (n - 160))) / 160
    return [max(-32768, min(32767, round(v * g))) for v in x]


def fft(a):
    n = len(a)
    if n == 1: return a
    ev, od = fft(a[0::2]), fft(a[1::2])
    t = [cmath.exp(-2j * math.pi * k / n) * od[k] for k in range(n // 2)]
    return [ev[k] + t[k] for k in range(n // 2)] + [ev[k] - t[k] for k in range(n // 2)]


def shares(x, cuts=(300, 500, 800)):
    """Share of energy above each cut, from one zero-padded FFT of the whole sound (DC and Nyquist bins included in the total)."""
    n = 1
    while n < len(x): n *= 2
    X = fft([complex(v) for v in x] + [0j] * (n - len(x)))
    p = [abs(X[k]) ** 2 for k in range(n // 2 + 1)]
    tot = sum(p)
    return [sum(p[k] for k in range(n // 2 + 1) if k * RATE / n > c) / tot for c in cuts], \
        sum(k * RATE / n * math.sqrt(p[k]) for k in range(n // 2 + 1)) / sum(math.sqrt(v) for v in p)


def butter_q(order):
    """Q of each second-order section of a Butterworth filter of even order."""
    return [1 / (2 * math.cos(math.pi * (2 * k - 1) / (2 * order))) for k in range(1, order // 2 + 1)]


def biquad_hp(fc, q_lin, rate=RATE):
    """The Web Audio BiquadFilterNode 'highpass' coefficients (Audio EQ Cookbook; the node takes Q in dB, q_lin = 10^(Q/20))."""
    w0 = 2 * math.pi * fc / rate
    al = math.sin(w0) / (2 * q_lin)
    c = math.cos(w0)
    a0 = 1 + al
    return [(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0], [1.0, -2 * c / a0, (1 - al) / a0]


def highpass(x, fc, order, tail=0.1, rate=RATE):
    """The speakers' imitation: order/2 biquad high-passes in series, zero start state, plus a tail of silence."""
    y = [float(v) for v in x] + [0.0] * int(tail * rate)
    for q in butter_q(order):
        b, a = biquad_hp(fc, q, rate)
        x1 = x2 = y1 = y2 = 0.0
        out = []
        for v in y:
            o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
            x2, x1, y2, y1 = x1, v, y1, o
            out.append(o)
        y = out
    return y


def level(x):
    """Loudest 1600-sample (50 ms) window's RMS in dBFS (the level rule's measure)."""
    acc = sum(v * v for v in x[:1600]); best = acc
    for i in range(1600, len(x)):
        acc += x[i] * x[i] - x[i - 1600] * x[i - 1600]
        best = max(best, acc)
    return 20 * math.log10(max(1e-12, math.sqrt(best / min(1600, len(x)))) / 32768)


def envelope(x, period):
    """Amplitude envelope: centred moving RMS over one period, times sqrt(2)."""
    h = period // 2
    sq = [0.0]
    for v in x: sq.append(sq[-1] + v * v)
    n = len(x)
    return [math.sqrt(2 * (sq[min(n, i + h + 1)] - sq[max(0, i - h)]) / (min(n, i + h + 1) - max(0, i - h))) for i in range(n)]
