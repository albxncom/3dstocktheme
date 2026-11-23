# 3ds-badges

Adds one badge set to the HOME Menu's badge collection without rebuilding it, and removes the last set again. Every badge that exists keeps its record, its IDs, its index and its images, so its placements in the live layout and in the eight saved layouts stay valid. Also checks that every placement in the live layout and in the saved layouts points at a badge of the collection. Stdlib only (tested with Python 3.14).

Status: built and checked on the Mac on copies (2026-11-12): the edits on decrypted copies, the way back into an encrypted extdata folder on a copy of the card's backup. Console test B4 passed on 2026-11-13, on one console: HOME showed a set of two badges this tool appended, they could be placed, the old sets and the saved layouts stayed as they were, and after `remove-last` `BadgeData.dat` was byte for byte as before the test and no layout missed a badge. Never put its output on a card without a backup of the badge data.

Nothing here writes to an SD card. `list`, `append`, `remove-last` and `check-slots` work on decrypted copies and need no keys. `decrypt` and `encrypt` run save3ds with the console's keys (passed by path, never copied) on copies the tool makes itself; copying the result onto the card is a separate, manual step (below).

## Data dir

Pass `--data DIR`, or set `BADGES_DATA` in the environment or in the repo's `.env` (see `../../.env.example`).

| Path | What | Written by |
| --- | --- | --- |
| `work/<step>/` | a `decrypt` output: `000014d1/` (extract of the badge extdata: `icon`, `boss/`, `user/BadgeData.dat`, `user/BadgeMngFile.dat`), `0000165c/` (extract of the layout extdata, `user/MngFile.dat`), `sd/` (the encrypted copy it read, as an SD root), `encrypted.sha256` | `decrypt` |
| `work/current/000014d1/`, `work/current/0000165c/` | the default `--in` and `--slots`: copy or move a decrypt there, or pass `--in` and `--slots` | you |
| `<tree>.encrypted/` | an `encrypt` output: `Nintendo 3DS/<id0>/<id1>/extdata/00000000/000014d1/` (the folder for the card) and its `encrypted.sha256` | `encrypt` |
| `<in>.append-<set id>/`, `<in>.remove-<set id>/` | the new extdata tree, next to the input; the input is never changed | `append`, `remove-last` |
| `sets/<name>/` | set folders of PNGs (any path works; this is where the checklists put them) | you, `fixtures.py set` |
| `manifest.json` | which set folder has which set ID, which PNG tile has which badge ID | `append` |
| `undo/<set id>-<hash>.json` | the bytes an append replaced, for `remove-last`; one record per input and result (the hash covers the sha256 of both files before and after), never overwritten | `append` |

The data dir holds decrypted console data: the NNID field, and in `undo/` the earlier bytes of the replaced areas, which in files written by Anemone3DS are leftover memory of the console. Keep all of it out of every repo.

## Getting the files, and putting them back

Needs [save3ds](https://github.com/wwylele/save3ds) v1.4.0 (latest release on 2026-11-12) as the macOS arm64 build that 3DSort v1.3.0 ships in its release asset (no FUSE needed; `decrypt` and `encrypt` refuse any binary with another sha256, `f386b689...`, because it receives the keys), and the console's `boot9.bin` and `movable.sed`. `KEYS` below stands for `--save3ds BIN --boot9 FILE --movable FILE`.

1. Back up the card: copy its `Nintendo 3DS/<id0>/<id1>/extdata/00000000/000014d1` and `.../0000165c` folders into a new backup folder under the same relative path (so the backup is an SD root), with a sha256 list. The card is only read.
2. `decrypt --sd BACKUP --out "$DATA/work/<step>" KEYS`: copies both extdata folders out of the backup (which it only reads, and checks unchanged afterwards), extracts them with `save3ds --extract`, and keeps the encrypted copy in `<step>/sd/`.
3. `list`, `append` or `remove-last`, `check-slots` with `--in <step>/000014d1` (and `--slots <step>/0000165c`).
4. `encrypt --from "$DATA/work/<step>" KEYS <step>/000014d1.append-7000`: copies `000014d1` from `<step>/sd/` into a new folder, extracts it and checks that it decrypts to `<step>/000014d1` (so the encrypted base and the edited tree belong together), runs `save3ds --import` with the tree, extracts again and checks that the result decrypts to the tree byte for byte, then writes `<tree>.encrypted/` with the folder and its `encrypted.sha256`. It refuses a tree whose files and folders differ from the decrypt's (save3ds imports every entry, `.DS_Store` included) or that changes anything other than `user/BadgeData.dat` and `user/BadgeMngFile.dat`. Only `000014d1` is ever copied or imported; `0000165c` is not in the output.
5. On the card (manual, with the card in the Mac and the console off): replace the card's `000014d1` folder with the one in `<tree>.encrypted/` (`rm -rf`, then `cp -R -X`, so no AppleDouble files land on the card), check it against `encrypted.sha256` and count the files. Recovery at any point: the same with the backup's folder.

What changes in the encrypted folder: `save3ds --import` first deletes every file of the extdata, then creates the tree's files anew (save3ds v1.4.0 `save3ds_fuse/src/main.rs`, `import` and `clear_impl`, verified in source). So every file in `000014d1/00000000/` is rewritten: `00000001` (the extdata's file system) and one container per file (`icon`, `BadgeData.dat`, `BadgeMngFile.dat`). Which container number holds which file depends on the folder written into as much as on the tree: deleting a file puts its entry on a free list, and each new file takes the entry freed last (last in, first out; `libsave3ds/src/fs_meta.rs` `remove` and `add`, verified in source), in the order the Mac lists the tree. So an import may renumber the containers. On this console's copy the card had 2 = `BadgeMngFile.dat`, 3 = `icon`, 4 = `BadgeData.dat`, after the import 2 = `icon`, 3 = `BadgeData.dat`, 4 = `BadgeMngFile.dat` (observed 2026-11-12). That is harmless: HOME opens the files by path (`BADGE:/BadgeMngFile.dat`, `BADGE:/BadgeData.dat`, verified in its code, path table 0x33d144), and paths resolve to containers through the file system in `00000001`. 3DSort's writes renumbered this card the same way: a copy of 2026-11-11 has 2 = `icon`, 3 = `BadgeMngFile.dat`, 4 = `BadgeData.dat`, the card on 2026-11-12 the numbering above, while Anemone3DS writes the existing files in place (`source/fs.c`, verified in source) and HOME has run on the renumbered card since (observed). New containers carry save3ds's unique ID `0xDEADBEEF` (`libsave3ds/src/ext_data.rs`, `new_sub_file`). Two imports of the same tree into the same base give the same bytes; an import into an already imported folder gives other bytes with the same content. No other file or folder is touched.

Where this differs from 3DSort's write (used by `home-layout/apply.py`, which already wrote this console's badge extdata on 2026-11-12, and HOME has used and rewritten it since): 3DSort runs the same `save3ds --import` of a full extract tree, but straight onto the mounted card, then extracts from the card to verify, with a journal for an interrupted write; before it writes it re-extracts the card and refuses if the badge files changed since its import. Here the import runs only on a copy under `work/`, the check that the base is still the card's state is the backup's sha256 list compared with the card right before the copy, and the card gets a plain folder replace checked by sha256, undone by copying the backup back. Same save3ds build, same import, same tree layout; 3DSort also writes the HOME extdata and a NAND payload, this writes only `000014d1`.

## Usage

```sh
python3 -I badge_sets.py list        --data "$DATA"                     # sets, counts, limits, NNID field set or not
python3 -I badge_sets.py append      --data "$DATA" /path/to/sets/MySet # -> work/current/000014d1.append-7000/
python3 -I badge_sets.py remove-last --data "$DATA" --in "$DATA/work/current/000014d1.append-7000"
python3 -I badge_sets.py check-slots --data "$DATA" [--in TREE] [--slots TREE]
python3 -I badge_sets.py decrypt     --sd BACKUP --out "$DATA/work/<step>" KEYS
python3 -I badge_sets.py encrypt     --from "$DATA/work/<step>" [--out DIR] KEYS TREE
```

- `list` prints one line per set (record slot, set ID, badge count, first index, placed badges, name, and whether this tool appended it), the totals against HOME's limits (100 sets, 1000 badges, 360 placed) and warnings when the placement counts disagree with the layout (HOME recounts them). The NNID field is only shown as set or not set.
- `append SET_DIR [--name NAME] [--out TREE]` appends the set and prints every new badge with its index and badge ID. It refuses when the set is already in the collection, when a limit would be exceeded, when `--out` exists, and when the input does not look as expected (below).
- `remove-last [--out TREE] [--allow-home-changes] [--undo RECORD]` removes the last set if this tool appended it (its set ID is in `manifest.json` and an undo record of it exists). By default it takes only files that are exactly what a recorded append wrote (sha256 of both files) and then checks the result against the record's sums from before the append. Files changed since (HOME rewrites `BadgeMngFile.dat` when it runs) need `--allow-home-changes`: the set's areas must still hold exactly what the append wrote, they and the case's view get their earlier bytes back, everything else is kept, and "byte-identical" is printed only if the result's sha256 equals the record's. When several records fit (appends onto inputs that differ only inside the replaced areas give one output), it refuses and asks for `--undo` with the record the append printed. It refuses when a badge of that set is placed: put it back into the case on the console first.
- `check-slots` checks the live layout and the eight saved layouts (test B2), see below.
- `decrypt` and `encrypt`: see "Getting the files, and putting them back". Both write only new folders next to `--out` (a hidden working folder, renamed at the end, removed on failure).
- Exit codes: 0 done, 2 refused or error (message on stderr), and for `check-slots` 1 when a placement in the live layout or a saved slot points at a missing badge.

## Set folders

A set is a folder of PNG files, as Anemone3DS takes them from `/Badges/<Set>/`: every PNG whose sides are multiples of 64 is cut into 64x64 badges, column by column (a 64x192 banner gives top, middle, bottom), each named after the file up to its first dot; `_seticon.png` (48x48) is the set icon. Without one, the first badge scaled to 48x48 is the icon. The set's name is the folder name unless `--name` gives one (at most 68 UTF-16 units, as are badge names). Files are taken in name order; hidden files (`.DS_Store`, `._*`) are skipped; anything else that is not a PNG, zip sets and subfolders are refused. No shortcut badges (every badge's shortcut is "none").

PNG reading is the tool's own: 8-bit grey, RGB, palette, grey+alpha and RGBA, non-interlaced, with `tRNS`; 16-bit, low bit depths and interlaced files are refused with a message saying how to save them.

`python3 -I fixtures.py set DIR --badges 2 [--icon]` writes a test set of generated pattern badges.

## IDs (manifest)

- A set gets a set ID the first time its folder (by name) is appended; every (file, tile) of it gets a badge ID. Both are stored in `manifest.json` and never change and are never reused, so appending the same set again (after `remove-last`, or on another copy of the files) gives the same IDs. A file added to the folder later gets new IDs at the end of the set; a file that is gone keeps its IDs reserved.
- Set IDs come from 0x7000 to 0x7FFF, badge IDs from 0x4000 to 0x6FFF. Anemone3DS and Advanced Badge Editor number sets 1 to 100 and badges 1 to 1000 (badge ID = position + 1), GYTB uses set ID 0xEFBE and badges 1 to n; the console's own collection (built by Anemone3DS) uses sets 1 to 3 and badges 1 to 288. None of these meets the two ranges, so a later Anemone3DS rebuild can never give one of its badges the identity of one of ours: a saved layout that holds our badge then empties that placement instead of showing another badge. Both ranges stay below 0x8000 in case some code reads them as signed or 16-bit. `append` also refuses if an ID is already in the file.

## What `append` changes, and the checks around it

For a collection with `s` sets and `u` badges, appending a set of `n` badges writes only:

- `BadgeMngFile.dat`: the header's set count (0x4, +1), badge count (0x8, +n) and total (0x18, +n x 0xFFFF); the used-badge bits u to u+n-1; the used-set bit s only if the set before it has its bit set (the console's Anemone3DS sets have none); badge records u to u+n-1 (badge ID, set ID, index, sub 0, placed 0, quantity 0xFFFF, flags 0, no shortcut, unknown word 0); set record s (set ID, set index s, start u, counts, the fixed words as Anemone3DS writes them).
- `BadgeData.dat`: set name slot s and badge name slots u to u+n-1 (the same name in all 16 languages), set icon slot s, badge image slots u to u+n-1 (64x64 and 32x32).

Not touched: the NNID field (copied through, never printed), the placed count (0xC), the case's view (0x10, 0x14), every existing record and image, the layout table.

Before anything is written, `append` checks the finished buffers against the input: every byte outside those ranges is equal, inside the bit fields only the new bits differ, the header moved by exactly those amounts, and a fresh read of the output shows every old set, badge and placement field for field unchanged and the new set as intended. Any difference refuses the append. It also refuses an input that does not look as expected: wrong sizes, header word 0 not zero, counts above the limits, used-badge bits that are not exactly 0 to count-1, a set count that disagrees with the set records, sets whose badges are not one block of indices in record order, set or header totals that disagree with the quantities, repeated identities. The output tree is written to a temporary folder, read back and compared, then renamed into place.

`remove-last` writes the bytes the append replaced back into exactly those ranges (from the undo record, which also holds a hash of every range as the append left it, so a changed set is refused and the differing ranges named) and sets the case's view back to its state before the append, since HOME may have pointed it at the set being removed. Given exactly the append's output, the result must equal the record's sha256 of both files from before the append, or nothing is written. Given files HOME changed since (only with `--allow-home-changes`), those changes outside the set's areas and the view are kept, and the result is called byte-identical only if its sha256 still equals the record's.

## `check-slots` (test B2)

Reads the live `BadgeMngFile.dat` and the eight full copies of it that the saved layouts keep in `MngFile.dat` (extdata 0000165c), and prints for each: whether the slot is saved, how many placements it holds, how many are folder icons, how many name no badge of the live collection by badge ID, set ID and sub ID ("missing": HOME empties those when it loads the slot), and how many match by those but differ in the rest of the identity ("index differs"). Unsaved slots are shown but do not count. Run it after an `append` with `--in` on the new tree to see that every placement still resolves.

## Format facts and their evidence

Evidence classes: verified (in the EUR 11.17 HOME Menu code, in a tool's source, or on the console's own decrypted files, with address or file), documented (3dbrew, Nintendo's manual), reported (another project's console tests), inferred (own reasoning). HOME code addresses are virtual addresses in the EUR 11.17 HOME Menu code.

| Fact | Evidence |
| --- | --- |
| Badge extdata `000014d1` holds `BadgeData.dat` (0xF4DF80) and `BadgeMngFile.dat` (0xD4A8); HOME errors on another size | documented (3dbrew Home_Menu); verified: HOME's loader at 0x27002c requires size 0xD4A8 and u32 0 at offset 0 |
| `BadgeData.dat` layout: set names 0x0, badge names 0x35E80 (16 languages x 0x8A, UTF-16), set icons 0x250F80 (0x2000), badge images 0x318F80 (0x2800: 64x64 RGB565 + A4), small images 0xCDCF80 (0xA00: 32x32 RGB565 + A4) | documented (3dbrew); verified: HOME's readers use these bases (0x26fc98, 0x26fda4, 0x26ffc4, 0x270248) |
| `BadgeMngFile.dat` header (0x4 sets, 0x8 unique badges, 0xC placed, 0x10 selected set, 0x14 selected column, 0x18 total, 0x1C NNID), used-badge bits 0x358, used-set bits 0x3D8, badge records 0x3E8 (0x28), set records 0xA028 (0x30), layout 0xB2E8 (360 x 0x18) | documented (3dbrew); verified on the console's file (all counts consistent) |
| Badge record: identity (unknown u32, badge ID, set ID, u16 index, u16 sub), u16 placed, u16 quantity, u32 flags, u64 shortcut twice | documented (3dbrew); verified in Anemone3DS v3.0.1 `source/badges.c` and on the console's file |
| Set record: 0xFF x 8, 0, 0x2710, set ID, set index, 0xFFFFFFFF, unique count, total, start index, 0, 0 | documented as "usually" (3dbrew); verified: Anemone3DS writes exactly this, and so does the console's file |
| HOME never writes `BadgeData.dat`; it writes `BadgeMngFile.dat` (placements, flag bit 0x4 of a badge record, the NNID field of a fresh file) | verified in the HOME code (accessor 0x1f2bb0 only reached in read mode; writer 0x219868), not proven exhaustive |
| HOME compares the NNID field with the active account's ACT data block 0xC and hides badges on a mismatch | verified up to the comparison (0x26fa30) and the follow-up routine 0x1fe21c; the on-screen effect inferred |
| A placement is matched by badge ID, set ID and sub ID when a slot loads, and emptied if no badge matches; another lookup also checks the index | verified (0x1b8f10, 0x26fd60 at 0x1b90bc; lookup 0x1fdbf8) |
| Each saved layout keeps a full `BadgeMngFile.dat` copy at 0x2ED10 + slot x 0xD4A8 in `MngFile.dat` (0x4D1280); loading copies only its placed count and layout table into the live file | documented (3dbrew); verified (0x1b9ac8 save, 0x1b8820 load) |
| `MngFile.dat` bytes 0x4 to 0xB: one "saved" flag per slot | documented (3dbrew); consistent with the console's file: the five flagged slots hold placements, the other three are empty |
| A free layout record is `00000000 ffffffff ffffffff ffff0000 ffffffff ffffffff`; all-zero means never used | reported (3DSort v1.3.0 `docs/BADGES_TESTING.md`, HOME's own writes on a USA New 3DS) |
| Folder icon placements have folder 0xF0FF and the folder tile's position | documented (3dbrew); reported (3DSort) |
| Limits: 1000 badges, 100 sets, 360 placements | documented (manual, 3dbrew); verified: bound checks `cmp #0x3e8` (0x1c8ea0, 0x1fdbc8), slot load rejects more than 0x168 placed (0x1b8f48) |
| Images: 8x8 tiles row by row, Morton order inside a tile, RGB565 and A4 by truncation, the low nibble for the even pixel; 32x32 = truncated mean of each 2x2 block | documented (3dbrew, "tiled"); verified: this converter reproduces all 288 badges of the console's collection byte for byte from the card's PNGs (64x64 and 32x32, colour and alpha), which Anemone3DS wrote |
| Set icon: 48x48 in the top-left of a 64x64 tiled RGB565 slot, no alpha | verified in Anemone3DS (`pngToRGB565` takes only 48x48 set icons, writes 0x2000 bytes) and on the console's file (the 48x48 area equals Anemone3DS's default icon through this converter); 3dbrew documents a 64x64 slot |
| Quantity 0xFFFF per badge, set total and header total = sum of quantities | verified in Anemone3DS and on the console's file; reported (3DSort: quantity is the total owned) |
| HOME recounts placements (header 0xC and each badge's placed count) | verified up to the zeroing loop at 0x1c8e1c; the recount inferred; the console's file after an Anemone3DS install is consistent again |
| `save3ds --import` deletes every file of the extdata and creates the tree's files anew; a file's container is `<extdata>/00000000/<entry + 1>`; freed entries are reused last in, first out, so the numbers depend on the folder written into; new containers get unique ID 0xDEADBEEF | verified in save3ds v1.4.0 source (`main.rs` `import`, `ext_data.rs`, `fs_meta.rs` `remove`, `add`); observed on a copy of this console's backup (all four files rewritten, renumbered) and on the card itself (renumbered by 3DSort's writes between 2026-11-11 and 2026-11-12) |
| HOME accepts badge extdata written by `save3ds --import` | reported: 3DSort's write of 2026-11-12 02:24 imported this console's `000014d1` (its transaction folder holds the base, desired and verify trees) and HOME has run on it since (it rewrote `BadgeMngFile.dat`) |
| The 0x20 to 0x358 area of `BadgeMngFile.dat` is zero; unused name and image areas of `BadgeData.dat` are not (Anemone3DS's "zeroing" writes from a stack address) | verified (console's file; Anemone3DS `source/fs.c`) |

## Guessed or single-source

The tool's design rests on these. Console test B4 (2026-11-13, one console) bore out the first two: a set with the ID 0x7000 and the badge IDs 0x4000 and 0x4001, appended after the last used index, was shown and its badges placed. The third stays open: no set on that console had its used-set bit set.

- HOME shows a set whose set ID (0x7000 and up) and badge IDs (0x4000 and up) are not small numbers. guessed: only IDs up to 1000 and GYTB's set 0xEFBE have been seen working (reported, 3DSort); nothing seen in the HOME code uses an ID as an index.
- New badges directly after the last used index, new set record and set index = the set count. inferred from HOME bounding its loops by the counts (0x26fdc4) and from Anemone3DS (single source); 3DSort reports HOME accepting five badges appended to an existing set.
- The used-set bit: the new set copies the bit of the set before it (clear on this console). guessed: the console shows Anemone3DS's sets without any set bit (verified on its file), so HOME seems not to need it; whether a mixed pattern matters is unknown.
- HOME shows only the top-left 48x48 of a set icon. guessed: Anemone3DS leaves leftover pixels of its last badge in the rest, this tool writes black there.
- HOME picks up a changed collection at the next boot. inferred; reported (3DSort: injected files accepted after a boot).
- The flags word 0 and the identity's unknown word 0 are fine for new badges. single source: Anemone3DS writes both and its badges work on this console; official badges carry a hash in the unknown word (reported, 3DSort).
- Removing the last set needs nothing beyond restoring its ranges and the case's view. inferred (no index of another badge moves).

## Tests and fixtures

```sh
python3 -I -m unittest discover -s . -p "test_*.py"
```

The tests use generated data only (`fixtures.py`: a stand-in for save3ds that keeps extdata as XORed numbered files and imports the way save3ds does, delete all then recreate; a collection laid out the way Anemone3DS writes one, with random bytes in its unused areas, the eight saved layouts, generated pattern PNGs in every supported colour type and filter). They cover the PNG reader and its refusals, the tiling against Anemone3DS's formulas, the strict reader and its refusals, `append` (every pre-existing record, name, image slot and the layout byte for byte, the new set's fields, IDs from the manifest, limits, refusals, and that the invariant check inside `append` stops a write outside the declared ranges), `remove-last` (sha256-identical after one and after two appends, changed files refused without `--allow-home-changes` and kept with it, a view on the removed set, two inputs that give one output each with its own record and `--undo`, a reused record only when identical, refusals), `check-slots`, `decrypt` and `encrypt` (the source only read, the round trip, the refusals: other files in the tree, a changed `icon`, a base that does not match its decrypt, a failed or wrong import, an unknown save3ds binary, two id1 folders) and the fixtures CLI. A separate test reads the header (0x4, 0x8, 0x18), the set records (ID, index, count, copies, first index), the badge records, names and images back with offsets written out from 3dbrew and Anemone3DS v3.0.1 `badges.c`, not with the tool's constants, on the fixture and on an `append` result, so a wrong constant fails even though the fixtures use the tool's own; with `BADGES_DATA` set (environment or `.env`) it also reads the console's decrypt in `work/current/` that way (3 sets, 243, 11 and 34 badges from 0, 243, 254, 288 in total), otherwise that one is skipped. The real save3ds round trip on a copy of the console's backup is not part of the tests (it needs the keys); it was run by hand on 2026-11-12 (see Status).

An independent reader as a second opinion: 3DSort's `core/badges.py` (GPL-3.0, not part of this folder) reads the same files; on 2026-11-12 it agreed with `list` on the console's collection and showed the old sets, badges and placements unchanged plus the new set after an `append` on a copy, with both files sha256-identical to the originals after `remove-last`.

## Sources

- 3dbrew "Home Menu" (badge extdata, BadgeData.dat, BadgeMngFile.dat, MngFile.dat), "Nintendo Badge Arcade/PrizeCollection" (64x64 and 32x32 images, the 32x32 one used in HOME's badge picker).
- Anemone3DS v3.0.1 (`source/badges.c`, `source/conversion.c`, `source/fs.c`): what it writes, read as a reference; no code taken. The converter here follows the same arithmetic so that its output equals Anemone3DS's.
- GYTB and Advanced Badge Editor, read for the IDs they hand out; Simple Badge Injector 1.4.2 (it only injects prepared files).
- 3DSort v1.3.0 `docs/BADGES_TESTING.md` (console tests of HOME's badge writes).
