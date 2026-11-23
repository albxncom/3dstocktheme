# Sound generators

The scripts that made three of the application's interface sounds, `back.wav`, `popup.wav` and `error.wav` in `uikit/sound/`. The other interface sounds are pack files put through `dsp.py`'s `convert` alone (at -20 dBFS, `move.wav` at -26), which gives the tracked files byte for byte from the decoded pack files, without a script of their own; the working loop has its own script beside the sounds (`uikit/sound/make_working.py`). Standard library only, Python 3.

| Script | What it makes |
| --- | --- |
| `make_back.py` | `back.wav`: a sine of 464 Hz under the envelope of `bong_001`, loudest 50 ms at -14 dBFS |
| `make_popup_error.py` | `popup.wav` and `error.wav`: the pack sound with its partials moved up (`build`), and the table the number of semitones was chosen from (`search`) |
| `measure.py` | a measurement table of any interface sounds (length, peak, level, partials over time, level through the speakers' imitation) against `back.wav` and `drop.wav` |
| `dsp.py` | the helpers they share: WAV in and out, the conversion, the level measure, the imitation of the console's speakers (a 6th-order Butterworth high-pass at 600 Hz) |

## The source files

The three sources are from Kenney's "Interface Sounds" (1.0), CC0, https://kenney.nl/assets/interface-sounds (zip of 834536 bytes, SHA-256 `f2193d072726d6758a5f7871b2dcc54dcce0d5c35c6f0a62f92549b327c81232`; licence and notice in `uikit/sound/CREDITS.md`). They are not in this repository. Each is decoded to 16-bit mono at 32000 Hz with ffmpeg (8.1.3 made the files below):

```
ffmpeg -i Audio/bong_001.ogg -ac 1 -ar 32000 -c:a pcm_s16le bong_001.wav
```

| File in the zip | SHA-256 of the `.ogg` | SHA-256 of the decoded `.wav` | Makes |
| --- | --- | --- | --- |
| `Audio/bong_001.ogg` | `d21d0f0b782445db579d11e2506b24cd1ac9d664ee33aeaf807761aa7b6fd710` | `e5c35bccd3df8b4b5892a4473708081090bbc82a46ffa5344e35c912e01f9a8b` | `back.wav` |
| `Audio/maximize_008.ogg` | `57c074e600e193a303ce781c04c0ccc191c6030f9f106594fd94a65e55435c17` | `68339ae27a639b54520bc33892d81a144be14f5d1c14eb49125e562c20f81d79` | `popup.wav` |
| `Audio/error_005.ogg` | `3f75b11df5b06d68d2c25fbf0e9c9d9e7dd9250562a4007ee34856b8a309a2ae` | `8475ff5758e6129c7508ce9283b0c28cea59b00cb07113e4484e82ee1eccf7a5` | `error.wav` |

Another decoder or ffmpeg version may decode Vorbis a little differently; then the result sounds the same but is not the tracked file byte for byte. Compare the decoded file's SHA-256 first.

## Making the tracked files again

From this folder, with the decoded files in `DEC`:

```
python3 -I make_back.py DEC/bong_001.wav OUT/back.wav
python3 -I make_popup_error.py build popup DEC/maximize_008.wav OUT/popup.wav 4 -14
python3 -I make_popup_error.py build error DEC/error_005.wav OUT/error.wav 14 -14
```

Each writes a file byte-identical to the tracked one (SHA-256 `back.wav` `e5aba55febd901a12f105fe8c4e0a48744dfeef6ca87f8bb9827782199a45350`, `popup.wav` `725694399ead535d38385d2ab3fc0aa827db407e6858b3c0fef625d69b55f1cf`, `error.wav` `393fbb8c598ddb935f58734695561f63fd3180c3185542a329423544e1ec06f6`) and prints its length, peak, level and, for popup and error, its level through the speakers' imitation against `back.wav`. `make_popup_error.py search DEC/maximize_008.wav DEC/error_005.wav` prints that for 0 to 24 semitones (a few minutes); the chosen numbers are the fewest that bring popup within 3 dB under `back.wav` and error from 1 dB under to 2 dB over it. `CREDITS.md` says in words what each script does.

## How a made file is approved

A measurement only says that the console's speakers can carry a sound, not that it is right. A new file is approved by ear on the console (a build with it installed and the sound played where it belongs), and only then replaces the tracked one. `make test` in the repository's root then reads it with the application's own reader (`test_audio`), which fails on a file it would not play and on an interface sound quieter than -34.4 dBFS through the same imitation of the speakers.
