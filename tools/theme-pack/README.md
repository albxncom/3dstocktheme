# 3ds-theme-pack

Builds custom 3DS themes into the HOME Menu's own theme system: one theme DLC title (a CIA) whose contents are your theme zips, so each theme shows up under "Change Theme" with its name and icon, can be deleted there one by one, and is stored by layout slots and shuffle like an official theme. Stdlib only (tested with Python 3.14), plus makerom and ctrtool (see Setup).

Optional since 3DStockTheme 1.0.0-beta.2: the application creates the pack on the console itself with the first theme (the TMD, the catalogue, a ticket when the console has none, and the record on the SD card), so a console needs nothing from this tool. It is still what builds a pack on a computer: many themes in one CIA, or a pack on a variation other than 09, which the application never writes. The application adds to a pack on 09 built here once its record is on the card ([Seeding the record from the Mac](../../docs/README.md#seeding-the-record-from-the-mac)). Its `verify` and `verify-delta` are also the independent checkers `make test` holds the core's packs and deltas against, and `adopt` brings the Mac's records level with what the application installed.

Status: a pack on the spare variation 09 lists, applies and comes back from a layout slot (console test T1), and HOME's own Delete removes one of its themes (T2), both on a console on 2026-11-12. Adding a theme without a reinstall (`delta` and the test application [`import-test/`](import-test/), console test T3) passed on a console on 2026-11-12 and 13: AM kept the contents installed before, HOME listed the old themes and the new one, and an old and the new theme applied.

## Warning: the target title

The pack is the EUR theme DLC title `0004008C000098xx`, variation `xx` = 0 to 9 (`--variation`, default 9). Nintendo's own themes live on some of these variations, and installers (FBI, custom-install, GodMode9) delete the title and its ticket before installing.

- Never target a variation that holds official themes: installing over it wipes your bought themes and their eShop ticket.
- Check first: on the SD card, `Nintendo 3DS/<id0>/<id1>/title/0004008c/` lists one folder per installed variation (`00009800`, `00009801`, ...). Pick a variation that has no folder; 09 is the usual spare one.
- Only EUR consoles: the catalogue is written for region EUR.

## Setup: makerom and ctrtool

The tools come from [Project_CTR](https://github.com/3DSGuy/Project_CTR), latest releases as of 2026-11-12. They go into `<data>/bin/` (or a folder given with `--bin`), never into this repo.

| Tool | Release tag | Asset | Asset sha256 | Binary sha256 |
| --- | --- | --- | --- | --- |
| makerom v0.19.0 | `makerom-v0.19.0` | `makerom-v0.19.0-macos_arm64.zip` | `ea222647bf362d1aa0d8b7570dc65e7968ce6b3d7b103e9cc815a99698d930e0` | `8eadfece7788c0c73ca48e5c35cabed0e704de0b2220e342f075a42494a7e78d` |
| ctrtool v1.3.0 | `ctrtool-v1.3.0` | `ctrtool-v1.3.0-macos_arm64.zip` | `d141ebe7e6b0f7b30b09f1903bc40beb91d3ceb79b4398c180f86c6831ed431a` | `e4bae2eb1b254af5f4849d5807c92b3caff768fab5d5ead5f50ca0fe4ac7ff81` |

Both tags point at commit `e8f5f529c54ff9b22a2491a480ffa69206bf7b19`. On another platform take the matching asset of the same releases (untested here).

```sh
DATA=/path/to/theme-pack-data
mkdir -p "$DATA/bin" && cd "$(mktemp -d)"
gh release download makerom-v0.19.0 -R 3DSGuy/Project_CTR -p 'makerom-v0.19.0-macos_arm64.zip'
gh release download ctrtool-v1.3.0 -R 3DSGuy/Project_CTR -p 'ctrtool-v1.3.0-macos_arm64.zip'
shasum -a 256 *.zip                    # compare with the table
unzip -o makerom-*.zip && unzip -o ctrtool-*.zip
shasum -a 256 makerom ctrtool          # compare with the table
mv makerom ctrtool "$DATA/bin/"
```

`build` and `verify` warn when a tool reports another version.

## Data dir

Everything the tool reads or writes lives in a data dir outside the repo: pass `--data DIR`, or set `THEME_PACK_DATA` in the environment or in the repo's `.env` (see `../../.env.example`).

| Path | What | Written by |
| --- | --- | --- |
| `themes/*.zip` | theme zips in the Anemone / Theme Plaza format: `body_LZ.bin`, optional `bgm.bcstm`, `info.smdh` at the zip root (`preview.png` is ignored) | you |
| `manifest.json` | which zip has which content index | `build`, `delta` |
| `out/theme-pack.cia` | the pack | `build` |
| `installed.json` | the TMD that is on the console, byte for byte, with its content records | `installed` |
| `out/delta/` | `tmd.bin`, `0.app`, one `<index>.app` per new theme, `delta.json` | `delta` |
| `bin/makerom`, `bin/ctrtool` | the tools | you (Setup) |

## Usage

```sh
python3 -I theme_pack.py build  --data "$DATA"     # -> out/theme-pack.cia, prints "index  name" per theme
python3 -I theme_pack.py verify --data "$DATA"     # re-reads the CIA through ctrtool, one line per theme, exit 1 on a mismatch
python3 -I theme_pack.py installed --data "$DATA" "$DATA/out/theme-pack.cia"   # after the FBI install: record what is on the console
python3 -I theme_pack.py delta --data "$DATA"      # -> out/delta/: add the new zips to the installed pack (see "Adding themes without a reinstall")
python3 -I theme_pack.py verify-delta --data "$DATA" [--base-cia CIA]           # checks out/delta independently, exit 1 on a mismatch
```

- `--variation N` (build): the theme DLC variation, 0 to 9. Without it: the manifest's, else 9. The first build records it in the manifest; a later build with another variation is refused, because the indices mean something only for that title.
- `--bin DIR` (both): where makerom and ctrtool are; default `<data>/bin`. The flag wins when given. There is no PATH lookup.
- BGM flag: HOME plays `bgm.bcstm` only if byte 0x5 of the decompressed `body_LZ.bin` is non-zero (3dbrew "Home Menu/Themes"). `build` warns, without changing the body, when a zip has `bgm.bcstm` but the flag is off, or the flag is on without `bgm.bcstm`; `verify` shows the flag per theme.

`verify` shares no parsing code with `build`: it extracts the CIA with ctrtool and checks with its own readers the title ID (TMD and ticket), the content indices (TMD, and ctrtool's own listing of the CIA header and TMD), the TMD hash chain and content hashes, the ticket rights 0 to 0x3FF, the content 0 files, header, languages and catalogue entries against the manifest, every theme's files byte for byte against its zip, the icons against the zips' `info.smdh`, and the size limits (body 0x150000, BGM 0x337000). The body limit holds for the file and for the size the LZ11 stream declares after decoding, because 0x150000 is the size of HOME's body buffer; `build` refuses a zip that breaks either.

## Content indices (manifest)

HOME, its save, shuffle and every layout slot reference a DLC theme by (variation, content index). So:

- a new zip gets the next unused index (several new zips: in file-name order), once; it keeps it forever;
- an index is never renumbered and never reused, also not after a zip is removed (`next_index` is a high-water mark);
- indices run from 1 to 349 (HOME's limit, see below); after 349 the pack is full;
- a zip that is removed from `themes/` keeps its manifest entry: its index is reserved, the pack lists it as removed but has no content for it, so HOME does not show it; put the zip back and it returns under its old index;
- the identity of a theme is its zip file name: renaming a zip makes it a new theme.

Removing a theme from the console needs no rebuild: HOME's own Delete removes one content. Adding a theme means a rebuild and reinstall of the whole pack (the installer wipes the title first; the fixed indices keep slots valid), or a delta imported with the test application, which passed console test T3 on 2026-11-12 and 13 (next section).

## Install (console)

Copy `out/theme-pack.cia` to its own folder on the SD card (not `/cias/`, which FBI's "install all" would sweep) and install it with FBI. Then record it on the Mac: `theme_pack.py installed <that CIA>`; `delta` builds on that record, and 3DStockTheme needs it on the card ([Seeding the record from the Mac](../../docs/README.md#seeding-the-record-from-the-mac)). Before the first install, back up the theme extdata `000002ce`, the HOME extdata `00000098` and the layout extdata `0000165c`. Recovery: GodMode9 deletes `title/0004008c/000098<xx>` and `extdata/00000000/000002ce` on the card (HOME rebuilds the theme extdata), FBI removes the pack's ticket.

**Never remove the pack in System Settings, Data Management.** It lists all theme titles as one entry (seen on a console on 2026-11-12: one "Design" entry for the four official theme titles and the pack together), so deleting it would very likely remove the official themes too (inferred, not tested). Remove the pack only with FBI: title and ticket of the pack's own title ID, after checking that FBI shows `0004008C000098xx` with the pack's variation. The same holds for the pack 3DStockTheme creates: its title is `0004008C00009809`.

## What the pack contains, and why

Content 0 (catalogue), NCCH CFA with an ExeFS `icon` and a RomFS:

- `MetaDataContentHeader.bin`: `1`, count, count, `0x00300030`, magic `0xDEADBEEF`, seven region records (EUR = region 2, primary language 1 English; the others `FFFF FFFF`), `8`, then the eight (region, language) pairs en, es, fr, de, it, nl, pt, ru. Byte for byte the EUR example on 3dbrew "Title list/DLC" with the counts replaced. "count" is the highest content index in the manifest.
- `ContentInfoArchive_EUR_<lang>.bin` for the eight languages, all identical: header `1`, count; then 0xC8-byte entries, entry *i* at 8 + 0xC8 × *i* for content index *i*: name (0x40 bytes) and description (0x80 bytes) as NUL-terminated UTF-8, cut at a character boundary, then u32 index and u32 enabled. Entry 0 is a disabled dummy. A reserved index gets an enabled entry "(removed)".
- `icons/<index>.icn` for every index 1 to count: the 0x1200-byte large icon of the zip's `info.smdh`, copied as is (both are 48×48 tiled RGB565); a grey icon for reserved indices.
- ExeFS `icon`: an SMDH named "Custom themes", which is what Data Management shows for the pack.

Content *index* (one per theme), NCCH CFA with only a RomFS: `body_LZ.bin` and, if the zip has one, `bgm.bcstm`, both copied from the zip unchanged. Name and description come from `info.smdh` title slot 0 (as Anemone reads it), falling back to the first filled slot and then the zip name.

makerom builds every NCCH (`-f cfa`, category AddOnContents, unique ID 0x98, no encryption) and the CIA (`-f cia -dlc -target t`), so the TMD marks contents 1 and up optional and TMD, ticket and NCCH headers carry signatures that only the console's signature patch (Luma3DS, always on) accepts; that is the same for every homebrew CIA. Two things are patched in makerom's output afterwards:

- makerom 0.19.0 crashes when the content indices have a gap (`ImportNcchContent` in `makerom/src/cia.c` opens `contentPath[i]` with the compacted position `i`). The builder passes positions 0 to n-1 with content ID = real index, then writes the real indices into the TMD chunk records, recomputes the TMD info-record hash and header hash, and rewrites the CIA header's content bitmap.
- The ticket's content rights are set to all of 0 to 0x3FF (makerom sets only the included indices), as custom-install's generic DLC ticket does, so a later partial install needs no new ticket.
- Content indices stop at 349, below the ticket's range: HOME 11.17 keeps one status byte per content index in a 350-byte row per variation (theme list loop at 0x1fbcec in the EUR 11.17 HOME Menu code, no range check), so an index of 350 or more would write into the next variation's row. Since indices are never reused, the pack is full once index 349 is handed out; `build` then refuses, and `verify` fails on any index above 349.
- HOME shows at most 100 themes in total, across all ten variations and official themes included (one total, counted by the theme list builder at 0x23fdb0 in the same code).

Sources and evidence:

- Verified in the EUR 11.17 HOME Menu code: the catalogue reader takes entry *i* at 8 + 0xC8 × *i*, fails if *i* exceeds the archive header's second word or if the entry's enabled byte is 0; the language picker matches the console's (region, language) against the list at 0x34 and falls back to the region records at 0x14; the icon reader reads 0x1200 bytes from `/icons/%u.icn`; HOME lists only contents that AM reports as installed and owned.
- Documented: 3dbrew "Home Menu/Themes" (control and theme content layout, body format) and "Title list/DLC" (header layout, EUR example, UTF-8 text, ExeFS icon of content 0); the makerom README (DLC index archive = icon + RomFS).
- Verified in source: Anemone3DS `dump_all_themes()` (catalogue entry by position, UTF-8 names, icon id from entry +0xC0, installed and owned filter) and `decompress_lz_file()` (LZ11); makerom `tik.c` and `cia.c` (ticket rights layout, CIA bitmap); custom-install's `basetik.bin` (rights 0 to 0x3FF).
- Not from any source, chosen here: the dummy entry's text, the "(removed)" placeholder for reserved indices, content ID = index, product code `CTR-N-THMP`, company code `00`, title version 0, the pack SMDH's text and colours; for deltas, content 0's ID `0x10000` + title version.

## Adding themes without a reinstall (delta)

`delta` writes what the console needs to go from the installed pack to the pack with the new zips, without deleting the title: `out/delta/tmd.bin` (the new TMD), `0.app` (the new content 0: catalogue and icons with the new entries) and `<index>.app` for each new theme, plus `delta.json` (what it holds and which installed TMD it was made from). The test application [`import-test/`](import-test/) imports it on the console.

1. After every install with FBI: `theme_pack.py installed <the CIA you installed>`. After a delta was imported without errors: `theme_pack.py installed "$DATA/out/delta"` (refused if that delta was not made from the current record, for example when it is recorded already).
2. Put the new zips into `themes/`, run `delta`, then `verify-delta --base-cia <the CIA the record came from>`.
3. Copy `out/delta/` to `sdmc:/theme-pack/delta/` and run the test application.

Design, and why:

- The installed contents come from `installed.json`, the TMD recorded from the CIA that was installed (or from the last imported delta), not from a rebuild: the new TMD must list each installed content with the ID, size and hash it was installed with, or it could not be that file (whether AM then keeps it is what T3 tests). `delta` still rebuilds every theme and refuses if a rebuilt content differs from its installed record (zip or tools changed): then only a full build and reinstall is consistent. The build is byte-deterministic for the TMD and the contents (tested: two builds of the fixtures, and a rebuild of a real two-theme pack against the CIA that was installed, match byte for byte; only the ticket's random title key, ticket ID and signature differ).
- The new TMD is the installed TMD with: every installed content record unchanged (also of contents HOME's Delete removed: a TMD that lists a content which is not installed is the normal state of DLC), a new content 0, the new themes (content ID = index, as in a full build), the title version one higher, and fresh info-record hashes.
- Content 0 gets a content ID no content of the title had (`0x10000` + the new title version; themes stay below `0x400`). AM stores a content as `<content ID>.app`; when an update is finalized it switches to the new files and deletes "the old /title files" (3dbrew "Titles", Data Structure, documented; which old files count as old is not said). Nintendo's CDN addresses a content by title and content ID (FBI's CDN installer, verified in source), so a changed content needs a new ID. Reusing ID 0 for a changed content 0 would put the new catalogue on the path of the installed one; what AM does then is unknown, so it is avoided.
- The title version goes up by one per delta: DLC titles receive updates with more contents (3dbrew "Titles", DLC Titles, documented), and an update is the case AM is built for. Whether AM requires the higher version is not known. A full build stays at version 0; FBI deletes the title first, so going back to 0 is fine. For the pack installed today nothing changes: it keeps version 0, content 0 with ID 0 and themes with ID = index, and its record is taken from its CIA as it is.
- Contents travel plain, exactly as in the CIA: the builder's TMD marks no content encrypted (type bit 0 clear, ctrtool shows "Encrypted: NO"), so AM has nothing to decrypt; the SHA-256 in the TMD is over these bytes.
- No ticket: the installed ticket grants content indices 0 to 0x3FF already (see above). Updates through Nintendo's servers do not reinstall tickets either (3dbrew "NIM Services", documented for system titles).

`verify-delta` shares no code with `delta` (it sits in the verify part, which the tests keep free of build code). It reads `installed.json`, the manifest and `out/delta/` with its own TMD parser and ctrtool, and checks: title ID, hash chain, title version + 1; signature and header bytes equal to the installed TMD except title version, content count and hashes; every installed record carried over unchanged; content 0's ID not used by an installed content; the TMD lists the installed contents plus every theme in `themes/`; the folder holds exactly the files to import; each file's size and SHA-256 match the TMD, no file is marked encrypted, theme contents are optional; content 0's catalogue (files, header, names, icons) against the manifest and the zips; each new theme's files against its zip. With `--base-cia` it also extracts that CIA with ctrtool, checks its TMD is the recorded one, and checks the kept contents against the new TMD and their zips. What it proves: the installed contents plus the delta files are a pack that is consistent with the new TMD and the manifest, and the tests show it equals a full build of the same themes except for content 0's ID and the title version. What it cannot prove: how AM on the console handles the import (that is console test T3).

## Test application (`import-test/`, console test T3)

A Homebrew Launcher application (`theme-import-test.3dsx`) that imports a delta into the installed pack. It works only on title `0004008C00009809` on the SD card (hard-coded, no way to pick another title) and never deletes a title or a ticket; removing the pack stays with FBI.

- Start: a read-only status screen of what AM reports: is the title installed, its TMD version, per content of its TMD the index, content ID, size and the installed and owned flags, and whether a pending import exists for this title.
- A: checks `sdmc:/theme-pack/delta/` without writing: the TMD's title ID and hash chain, every file's size and SHA-256 against the TMD, and the delta against the installed state (every installed record unchanged in the new TMD, a new content 0 ID, the title version one up, a file for every new content and none for an installed one). Then it shows the plan. A delta whose new TMD is already installed (phase 1 done, phase 2 failed earlier) gets a plan with phase 2 only.
- L+R+A: imports, in two commits, as FBI's CDN installer did for DLC (see below). Afterwards it reads the status again and prints per content: before, after, owned, and KEPT, LOST or NEW; after phase 1 it prints the same comparison.
- On a failure it cancels its own open content, TMD and title import and, if this title's import is still pending (or the query fails), removes that one pending import (`AM_DeletePendingTitle` for this title ID). It never calls a delete of all pending imports, never a title or ticket delete. What AM does with the installed contents on such a cancel is unknown, and in Azahar's AM a cancelled import whose TMD was accepted deletes the title's contents, so a failure in phase 1 may take the installed themes with it. The KEPT/LOST listing after the failure is what AM reports; the recovery is reinstalling the full pack with FBI (keep its CIA on the card in `/theme-pack/`).
- The header check: signature type, padding, issuer, format version 1, title type 0x40 and zero in every other field except title version, content count and hash, as in every TMD of this pack (the console has no copy of the installed TMD, so it checks the pack's fixed values; `verify-delta` compares with the recorded TMD).
- L+R+Y (shown only when this title has a pending import): removes that pending import.
- HOME and sleep are disabled during the import. Every AM result code is on the top screen and in `sdmc:/theme-pack/import-test.log` (appended).

AM call sequence (libctru 2.7.0 as in the image below; command headers from libctru `source/services/am.c` at tag v2.7.0 and 3dbrew "Application Manager Services"; FBI = `source/fbi/action/installcdn.c` at the commit before dd0ccff, the last version of its CDN installer):

| Step | Call (header) | Source for using it this way |
| --- | --- | --- |
| status | `amAppInit` (am:sys), `AMAPP_GetDLCTitleInfos` (0x10050084), `AMAPP_GetDLCContentInfoCount` (0x100100C0), `AMAPP_ListDLCContentInfos` (0x10030142) | HOME's theme list and Anemone3DS's theme dump use these for theme DLC; flags bit 0 installed, bit 1 owned (libctru `AM_ContentInfoFlags`) |
| status | `AM_GetNumPendingTitles` (0x000B0040), `AM_GetPendingTitleList` (0x000C0082), `AM_GetPendingTitleInfo` (0x000D0084), `AM_GetNumImportContentContexts` (0x000F00C0), `AM_GetImportContentContextList` (0x00100102) | 3dbrew: available on am:sys, am:u and am:net |
| phase 1 | `amInit` (am:net), `AMNET_InstallTitleBegin(SD, title, false)` (0x08040100) | FBI l.113 and l.250; on an installed DLC title in FBI's second step (l.113) |
| phase 1 | `AMNET_InstallTmdBegin` (0x080A0000), `FSFILE_Write`, `AMNET_InstallTmdFinish(h, true)` (0x080C0042) | FBI l.88, l.130, l.106 |
| phase 1 | `AMNET_InstallContentBegin(0)` (0x080E0040), `FSFILE_Write`, `AMNET_InstallContentFinish` (0x08120002) | FBI l.97, l.109: content 0 right after the TMD, no context call (AM makes contexts for non-optional contents at the TMD; Azahar's `EndImportTmd` does the same) |
| phase 1 | `AMNET_InstallTitleFinish` (0x08080000), `AMNET_CommitImportTitles(SD, 1, false, &title)` (0x080900C2) | FBI l.111-112 |
| phase 2 | `AMNET_InstallTitleBegin`, `AMNET_CreateImportContentContexts(n, indices)` (0x080D0042), per content Begin, `FSFILE_Write`, Finish, then TitleFinish and Commit | FBI l.113-114, then the same per-content path |
| log | `AMNET_GetNumCurrentImportContentContexts` (0x08130000), `AMNET_GetCurrentImportContentContextsList` (0x08140040) | shows which contexts AM holds after the TMD and after the context call |
| failure | `AMNET_InstallContentCancel` (0x08110002), `AMNET_InstallTmdAbort` (0x080B0002), `AMNET_InstallTitleAbort` (0x08070000), then `AM_DeletePendingTitle(SD, title)` (0x000E00C0, 3dbrew DeleteImportTitleContext) only for this title ID | FBI l.122-124 and l.210 |

Why two commits: FBI first imported DLC in one go and fixed it on 2016-05-28 (commit ed37b05, "Fix DLC installation from CDN") by committing after the TMD and content 0, beginning the title import again on the now installed title, and creating contexts for the optional contents; it shipped that until the CDN installer was removed for server-side reasons (dd0ccff, 2018-08-09). The second step, begin on an installed DLC title and import only some contents, kept content 0 there, which is the no-TMD half of the question T3 asks. The first step on a title that already has contents (a TMD import that does not re-import them) is the unproven half; T3 answers it, and the comparison after phase 1 shows it on its own.

Build, from `import-test/`:

```sh
docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm:20260610 make
```

Image `devkitpro/devkitarm:20260610` (the current tag on 2026-11-12, the same image as `latest`), digest `sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`; it holds devkitARM r68-1 and libctru 2.7.0-1. This writes `theme-import-test.3dsx` (plus `.elf`, `.smdh` and `build/`, ignored by git); copy the `.3dsx` to `sdmc:/3ds/`. `make clean` removes them. The delta check (`source/delta.c`) is plain C; `host/check.c` builds it for the Mac, which the tests do.

What the import rests on (verified = read in source or seen in a dump; documented = 3dbrew; reported = used by others; inferred = own reasoning):

- Beginning a title import on an installed DLC title and importing only some contents keeps the others: verified in FBI's source for the case without a TMD import (its second step kept content 0), reported by its use 2016 to 2018. With a TMD import (phase 1 here): no source shows it. 3dbrew "Titles" says only that the new files are written and "the old /title files are deleted" at the end; Azahar's AM (2126.2, `am.cpp` `CIAFile::Close` l.1006-1043) deletes old contents until it meets the first one whose ID the new TMD lists and then stops, which is no model of keeping. Not seen on a console. This is what T3 tests.
- The TMD import creates the import context for content 0 by itself, optional contents need `CreateImportContentContexts`: verified in FBI's sequence and Azahar's `EndImportTmd`.
- A new ID for content 0 and a higher title version: inferred from how updates are stored and served (above); whether AM needs either is unknown.
- What a cancel does to the installed title is unknown. Against "it touches only the import": in Azahar's AM, `CancelImportTitle` (l.4178-4197) drops the import, whose `CIAFile::Close` then deletes the title's whole `content/` folder when the import had an accepted TMD and is not a contents-only import (l.994-1001); FBI called `AM_InstallTitleAbort` after a failed second step on a DLC title it had just committed (l.210), with no record of what remained. For it: only the 3dbrew names (CancelImportTitle, DeleteImportTitleContext) and FBI's "Delete Pending Title" using `AM_DeletePendingTitle` (2.6.1, `deletependingtitles.c` l.39). So a failure after the TMD in phase 1 may cost the installed themes; the recovery is the FBI reinstall.
- `AMNET_CommitImportTitles`' third parameter is `temp` in libctru and "cleanup" on 3dbrew; it is false here, as in FBI, which in either reading leaves other imports alone.
- New contents are owned without a new ticket because the installed ticket grants indices 0 to 0x3FF: builder (verified), and seen on a console in T3: the content added after the install was installed and owned.

Limits:

- AM's handling of the import was seen on one console only (T3).
- It trusts that the delta files are the builder's plain NCCH contents; it checks their hashes against the delta TMD, while the TMD's own signature is fake as for every homebrew CIA (Luma's signature patch).
- At most 350 contents per TMD; one delta at a time; a content is imported in one piece (no resume inside a content: after a failure, check and import again).
- The status is AM's view (TMD and flags), not HOME's list; restart the console before looking at HOME's theme list.

## Tests and fixtures

```sh
python3 -I -m unittest discover -s . -p "test_*.py"
```

The tests cover the manifest rules, the content 0 writer, zip reading and limits, the fixtures, and that the verify code uses nothing from the build code. With makerom and ctrtool in `$THEME_PACK_DATA/bin` they also build and verify real packs in a temporary data dir (rebuilds, reserved indices, tampered packs, variations); otherwise those tests are skipped. The data dir itself is only read.

`test_delta.py` covers the delta path: the TMD writer against the verify reader; with the tools, a pack of two fixture themes recorded as installed plus a third zip: `delta`, `verify-delta`, the post-import contents against a full build, the record moving with each delta, the refusals and a tampered delta; with a C compiler (`cc`), the application's delta check from `import-test/source/delta.c` on synthetic deltas (SHA-256 at block edges, every plan and refusal) and on the real delta.

`fixtures.py DIR` writes two test theme zips to `DIR/themes/`, generated from the format documentation only: a red theme without BGM and a blue one with a `bgm.bcstm` stand-in (a CSTM magic and zeros, not audio; the body's use-BGM byte stays 0 so HOME ignores it). The bodies are solid-colour themes that meet every constraint 3dbrew lists; they have not been shown on a console or in an emulator, and the colour byte order (RGB) is a guess.

```sh
python3 -I fixtures.py /tmp/theme-dry-run
python3 -I theme_pack.py build  --data /tmp/theme-dry-run --bin "$DATA/bin"
python3 -I theme_pack.py verify --data /tmp/theme-dry-run --bin "$DATA/bin"
```

## Credits

- Howling Theme Tool (closed source) showed that a theme DLC on a spare variation works.
- 3dbrew documents the theme and DLC formats; Anemone3DS's official-theme dumper and LZ11 code, makerom and custom-install were read for the details above.
