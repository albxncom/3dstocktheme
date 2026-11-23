"""Makes back.wav: a sine of 464 Hz (phase 0) under the amplitude envelope of Interface Sounds' bong_001 (a nearly
pure 232 Hz tone the console's speakers do not play; its octave they do), then the conversion with the loudest 50 ms
at -14 dBFS.

  python3 -I make_back.py BONG_001.wav OUT.wav [LEVEL_DB]

BONG_001.wav is bong_001.ogg decoded to 16-bit mono at 32000 Hz (README.md). It is converted first as every
interface sound is (-20 dBFS), then its envelope (a centred moving RMS over one period of 232 Hz) carries the tone.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp import RATE, read_wav, write_wav, convert, envelope, level, shares

bong = convert(read_wav(sys.argv[1])[1], -20.0)
lv = float(sys.argv[3]) if len(sys.argv) > 3 else -14.0
env = envelope(bong, round(RATE / 232))
y = convert([env[i] * math.sin(2 * math.pi * 464 * i / RATE) for i in range(len(bong))], lv)
write_wav(sys.argv[2], y)
pk = max(abs(v) for v in y)
s, cen = shares(y)
print('n', len(y), 'peak', pk, '%.2f dBFS' % (20 * math.log10(pk / 32768)), 'level %.2f' % level(y),
      'clipped', sum(1 for v in y if v in (32767, -32768)), 'above300/500/800', [round(100 * v, 1) for v in s])
