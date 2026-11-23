# tools

Scripts that build, check and picture the application (`app/`, `core/`, `host/`, `uikit/`). Each runs from the repository's root and writes only into the folders it is given (and the tree's own `build/`, which git ignores). Nothing here talks to a console.

The kit in `uikit/` is a git submodule (its own repository, "3ds-uikit", pinned to one commit; `.gitmodules`, and the application's README for the clone, `git submodule update --init` and moving the pin). `build/checks.sh`, `device.sh` and `cia.sh` take the kit from the pinned commit, whatever the submodule's checkout holds at the moment: they stop with one line when the submodule is not checked out or does not hold that commit, and the image's name and `hygiene.sh` they use are their export's, the pinned kit's. `git archive` and `git ls-tree -r` stop at a submodule, so `build/common.sh` has `export_tree REV DIR` (the commit's files, and the kit's pinned commit from the checked-out submodule into its folder) and `tree_files REV` (every file's path, the kit's at the pin under its folder) for `checks.sh` and `device.sh`. `flows/record.sh` and `render.sh` say "with changes in the working tree" also for a kit checked out at another commit than the pin or with changed files in it.

## build/: the acceptance builds

The 3DS builds run in the container `devkitpro/devkitarm:20260610` (named once, in the kit's `uikit/tools/build/image.sh`, which `build/common.sh` reads), with the network off. makerom, ctrtool and bannertool are not in the repository: `THEME_PACK_BIN` names the folder that holds them (else `THEME_PACK_DATA/bin`, as in the Makefile). Every output folder must be new; the scripts never clear one.

- `build/checks.sh REV OUT`: the acceptance checks on two clean exports of a commit (`OUT/a`, `OUT/b`): `make app`, `DEMO=1`, `PROBE=1` with their warning lines, the two plain builds compared byte for byte, the banner and the CIA (its title ID), `make -B host`, `make test` and the theme-pack tests, the hygiene check before and after, tracked build output. `make test` and the theme-pack tests read `THEME_PACK_DATA` from the environment, or from `ENV_FILE` (a file of `VAR=value` lines, sourced for those two steps only; the values are never printed).

  ```sh
  THEME_PACK_BIN=<tools folder> ENV_FILE=<env file> tools/build/checks.sh HEAD <new folder>
  ```

- `build/device.sh REV OUT SPEC...`: `checks.sh` into `OUT` (summary in `OUT/summary.txt`), then the builds for the console from a third clean export (`OUT/d`), one per `SPEC` of the form `"<make options>|<launcher name>|<file name>|<build folder under app/>"`; each `.3dsx` is staged in `OUT/stage/` with its size, sha256 and SMDH title. About 35 minutes.

  ```sh
  THEME_PACK_BIN=<tools folder> ENV_FILE=<env file> tools/build/device.sh HEAD <new folder> \
    "|3DStockTheme G14|3DStockTheme-G14.3dsx|build" "PROBE=1|3DStockTheme G14 Probe|3DStockTheme-G14-Probe.3dsx|build/probe"
  ```

- `build/cia.sh OUT PLAIN.3dsx FILE.cia`: the CIA from the export `device.sh` built in, without rebuilding the ELF (checked by hash), staged as `OUT/stage/FILE.cia` with its title ID and product code. Then check it from `OUT/d` with `app/meta/logo/test_logo.py --cia` and `app/meta/test_banner.py --cia` (README of the application).

  ```sh
  THEME_PACK_BIN=<tools folder> tools/build/cia.sh <the device.sh folder> 3DStockTheme-G14.3dsx 3DStockTheme-G14.cia
  ```

- `build/notices.sh OUT`: writes the file of third-party notices that goes with a release: the versions and licence texts of the libraries out of the container the build links them from, quirc's and the icons' out of the tree (`docs/README.md`, "Release").
- `uikit/tools/build/hygiene.sh [DIR]` (the kit's, which `checks.sh` calls): what must never be in the repository (a home folder path, a local network address, and the words in `HYGIENE_WORDS`, an extended regular expression such as your own name), searched in `DIR` (default: this repository, the application's, with the checked-out kit in it: the script looks for the repository the kit is a submodule of, and searches the kit's own only where there is none; else the root of an export) in every file but `.git` and what the tree's `.gitignore` files leave out (the build output; the tracked `tools/build/` and the kit's `tools/build/` are searched); prints `file:line` of each hit, never its text. Exit 1 on any hit, 2 if the search fails (no such folder, a bad expression).

  ```sh
  HYGIENE_WORDS='<name>' uikit/tools/build/hygiene.sh
  ```

- `uikit/tools/build/alone.sh REV OUT` (the kit's): the proof that the kit stands alone: the kit's commit `REV` exported by itself into `OUT/kit` (`REV` a commit of the kit's or of this repository's, whose pin is then taken; `HEAD` is the submodule's checked-out commit), there `make test` on the Mac and `make example` in the container, and `hygiene.sh` on the export before and after; one line a step, the example's size and SHA-256, exit 0 only if all passed.

## flows/: the flow renderer

The application's own `main.c` and `app_ui.c` with the kit's `ui.c`, built on the Mac against the kit's recorder of citro2d (`rec.c`, `fake/`; these, `raster.py`, `pics.py`, `order.py` and the font's files are the kit's, in `uikit/tools/flows/`), walk the two flows the way a user presses the keys (`designs.c`, `badges.c`): every state of installing, removing and refusing a design or a badge set, and their errors. Each frame is written as a draw list and drawn into a picture by `raster.py` (text in the kit's renderer font, Nunito, every theme picture a plain violet square). Stock, colour scheme Weiss, German, the demonstration build. Content is stand-in only: theme zips from `theme-pack/fixtures.py` (`make_zips.py`) and the badge sets of `test_badgeflow fixture`. It needs Python 3 with Pillow and the host compiler `make host` uses.

- `flows/render.sh OUT`: builds the host library and the art in the tree, then the two drivers, runs the two walks and writes the 90 pictures (800 x 960, the top screen above the bottom one, enlarged twice) as `OUT/designs/*.png` and `OUT/badges/*.png` with `OUT/list.json` and `OUT/list.txt` (the label of each state, from `states.py`); the scratch goes into `OUT.work`. `OUT` may hold only letters, digits, `.`, `_`, `-` and `/`, at most 200 characters as an absolute path (the badge fixture's helper removes its folder with a shell command and holds the path in 300 bytes); a trailing slash is dropped. It runs the two rule checks over the draw lists: `order.py` (no text drawn after a picture's texture, no transparent piece) and `signs.py` (no sign drawn on both screens; exactly the expected states show none). Exit 0 only if all passed and there are 90 pictures. About a minute.

  ```sh
  tools/flows/render.sh <new folder>
  ```

- `flows/compare.py A B`: two such folders picture by picture; names each picture that differs with its count of differing pixels and their box per screen (true-size coordinates). Exit 1 if any differs. The check that a change leaves the screens as they were: render before and after, compare.

  ```sh
  python3 -I tools/flows/compare.py <folder before> <folder after>
  ```

- `flows/record.sh OUT`: the records of everything the application draws, for a change that must leave it all as it was (the library cut): the draw lists (every call with its numbers, colours, sprite and text) of both walks and of the sheet (`sheet.c`: the stylus on a list, its scroll bar and the settings rows, the settings screen, the 3D slider, every glass style and pattern, and each drawing function of `ui.h` the walks miss, with fixed arguments) in every look the tree builds, Stock in each of its schemes and every other look in Weiss and Nacht, as `OUT/lists/<look>-s<scheme>/<walk>/*.dl`; the sha256 of every generated art file for Stock with each subset of the other looks (`OUT/art.txt`); per test command of `make test` its "ok" lines counted and its summary lines (`OUT/tests.txt`; `THEME_PACK_DATA` from the environment or from `ENV_FILE`, as `checks.sh`). `COVERAGE=1` builds the drivers with clang's coverage and writes `OUT/coverage.txt` (`coverage.py`): per file of the drawing code the functions and lines executed by the walks alone and with the sheet, and by name what stays unexecuted. `OUT` as `render.sh`'s. Exit 0 only if every step ran and no test failed. About 10 minutes, most of it `make test`.

  ```sh
  ENV_FILE=<env file> tools/flows/record.sh <new folder>
  ```

- `flows/compare_records.py A B`: two such folders record by record: names each draw list that differs by its look, scheme, walk and state with its first differing line (a texture's pixels or the call's number, both lines), each art hash and test line that differs, and each record found on one side only. Exit 1 if anything differs.

  ```sh
  python3 -I tools/flows/compare_records.py <records before> <records after>
  ```

  The second form's files (below) are not compared here, only counted in a line of their own, so records with them compare against records made before they existed.

- The second form, for a change that may move sprites in the atlas but must leave every screen as it was (stage 3 of the library cut): `record.sh` also writes every draw list once more as `OUT/named/<look>-s<scheme>/<walk>/*.dl` (the kit's `rec.c`, `named_dump`) and per set of looks a sprite list `OUT/sprites/<looks joined by "-">.txt` (`sprites.c`, built against that join). In a second-form list:
  - an image call carries, in place of the texture's number and its coordinates, the name of the sprite whose place holds what it samples (`names.c`: the atlas's and the glass sheet's sprites by their `ART_<name>` without `ART_`, with `+dx,dy` where the call starts inside one; the selection's corners by theirs; the application's part's pictures (the turning house's sheet) `HOUSE`; a region of the atlas or the sheet no sprite's place holds `-atlas` or `-sheet`; any other texture `-`; a call of no area on the screen `-none` and one whose tint makes all four corners fully transparent `-clear`, both with no texels: they draw nothing, and a name the look does not draw, or `ui_text`'s transparent piece (the atlas's first 8 x 8 texels, drawn at alpha 0), would otherwise be named by whatever sprite stands at the atlas's corner), the texture's format, the region it samples (its size in texels, where it starts inside its first texel, mirrored on x and on y) and a hash of the region's texels; where the call filters linearly the region takes in the texel past each edge the filter reaches (`ring L T R B`, so a changed neighbour in the atlas shows: for each pixel whose centre lies in the call's quad the texel coordinate its centre maps to, and the two texels round it mixed; a texel past the edge counts where its weight is at least 1/256, under which a neighbour moves no 8-bit channel by a whole step; a turned call takes in all four, unweighed); a repeating texture (the pattern) keeps its size and coordinates and hashes all of it; everything else of the call stays as in the first form, with its depth (`z`) added, and `unflushed` where the texels differ from those of the texture's last flush (`C3D_TexFlush`, or `GSPGPU_FlushDataCache` over the part it covers), `deleted` where its texture was deleted before the call;
  - rectangles and text carry their depth too; there are no texture lines (`X`): no atlas size, no hash of a whole atlas;
  - `E` lines first: what happened to the textures since the list before (create with its format, delete, flush, cache flush with its offset and size), each texture by the number of its making in the run, with the frames before this one it happened in, the same event repeated in a row counted.
  - Textures made at run time (the camera picture, the wallpapers, the theme icons, the glass sheet, the house, the pattern tile, a look's own textures) are treated as the atlas is: they appear only through the calls that sample them, each with the hash of the texels it samples as they stand when the frame ends (when the GPU would read them), and through their `E` lines; their size shows only in what a call samples.
  - The sprite list: per look and scheme every sprite of the atlas and the sheet, the kit's and the application's part's (each read from its own texture's blob, `sprites.c`), by name with its size, pad, cap and a hash of its pixels (`-` for a name the look does not draw), the part's pictures (`HOUSE`, the turning house's sheet), the pattern tiles and the look's own blobs (by their bytes), the colours by name; per look the theme icon's corners; once the selection's corners. Nothing in it depends on an id, the order of the sprites or a place in the atlas.

- `flows/compare_named.py [--calls] A B`: the second comparison: each second-form list as two sequences, so that a texture event added or missing shifts no call: the calls (clears, rectangles, images, text) line for line (a list whose calls differ named by look, scheme, walk and state with its first differing call, its number among the calls and the sprite's name on each side; then over all differing calls the sprites whose calls differ, and in how many lists), and the texture events matched in order after the textures are renumbered past the application's part's (an event ending in `part-atlas` or `part-sheet`; `part-pictures`, made where the house's texture was, shifts no number, and an event that differs only by that word is counted as renamed, not changed), what is on one side only counted by kind (create, delete, flush, cacheflush, with the part's word) and the first shown per list; the sprite lists by look, scheme and name (a name on one side only is listed), `tests.txt` as `compare_records.py` does; `art.txt` and `lists/` are left to `compare_records.py`. It prints the differing call lines and texture-event lines in sum. Exit 1 if anything differs; with `--calls` only if a call line, a list, a sprite or a test line does (the proof for a change that adds or drops textures and must leave every call as it was).

  ```sh
  python3 -I tools/flows/compare_named.py [--calls] <records before> <records after>
  ```

  What it does not see:
  - the bytes of the generated files, the ids and the atlas's size (the first form's `art.txt` and `X` lines do);
  - which texture a call reads: two calls that sample equal texels with the same filter are the same, whichever texture holds them (a sprite moved from the atlas to a texture of its own included);
  - a neighbour in the atlas at a call that filters by nearest texel (it cannot reach it) and, at a linear call, a side the filter reaches with less than 1/256 or anything past the one texel; the GPU's own rounding of texture coordinates (the reach is worked out in floating point at the pixel centres); texels no call samples (a sprite's own pixels are in the sprite list even when no walk draws it);
  - a texture's texels before the frame's end (each call is hashed with the texels as they stand when the frame ends), and the order of texture events relative to the calls inside a frame (an event says only how many frames before the list it happened);
  - the plain build's and the probe's own code (the drivers are the demonstration build), and what happens on the console only: what the GPU makes of `z`, the data cache itself, the sound.

  What a new packing still showed, rightly, before every packed texture got its border: a sprite that moves onto or off an edge of its texture at a call whose filter reaches past it. At the texture's edge the GPU clamps (the sprite's own edge texel is read again), inside it reads the gap, which is transparent; so where the filter reaches, the pixels differ. In the atlas before the border `FOLDER_FOOT` stood at the left edge and `POUR_PIECE` (drawn turned) at the right one. Since then `art.py`'s `pack()` keeps `BORDER` (one texel, the filter's reach) free between every sprite and each edge of the atlas, the glass sheet and the corners' texture, and `test_atlas.py` in `make test` holds it, so a new packing is no difference here.

  Proven with spoils (2026-11-20, against two equal records of the same tree): two sprites of the same size exchanging places in the atlas are no difference here while the first comparison names 1105 records; the atlas twice as wide and the packer's order reversed for sprites of equal height are named only at sprites that moved onto or off a texture's edge on a side the filter reaches (62 and 166 records); one pixel of one sprite, a call drawing another sprite of the same size, the atlas without the gap between sprites (named at the linear calls only), a position and a colour are named by both. With the border (2026-11-21, against records of the same tree): the packer's order reversed and the atlas twice as wide are no difference here (0 records each) while the first comparison names 1931 of 1932.

- `flows/explain_edges.py A B`: for a change that takes sprites off their texture's edges (the border): whether every line in which the second-form lists of A and B differ is an image call of the same sprite on both sides, differing in the hash of its texels only, whose ring takes in a side on which the sprite touched its texture's edge in A's packing, while in B's it touches none. It reads the places from each folder's generated art (`A.work/art`, as `record.sh` leaves it). It prints the explained lines per look and sprite with the side, the number of lists and the screens (scheme, walk and state), then the unexplained ones (another kind of line, another sprite, a difference beyond the hash, a sprite at an edge in B, a list on one side only or of another length). Exit 0 if every differing line is explained, 1 if any is not, 2 if a folder lacks its art or its second form. The sprite lists and `tests.txt` stay `compare_named.py`'s.

  ```sh
  python3 -I tools/flows/explain_edges.py <records before> <records after>
  ```

- `flows/texture_changes.py A [B]`: what a change of textures costs, from the first form's lists (one frame each): per list how often two calls in a row read different textures (an image call its texture's number, a text call the font as one texture; clears and rectangles read none), on the console the end of one of citro2d's batches. With A alone the sum over all lists and the largest single list; with B too both, the rise in sum and the ten lists with the largest rise (`LIMIT`). It does not see a texture read once on whole pixels and once smoothed (the atlas and its smoothed twin share a number), the font's sheets, or a change of screen as anything but a change. Exit 0, 2 if a folder has no first form.

  ```sh
  python3 -I tools/flows/texture_changes.py <records> [<records after>]
  ```

## Other folders

- `theme-pack/`: the Python tool that builds the theme pack on a computer, the pack the application adds themes to (its own README).
- `badges/`: the Python tool that edits the HOME Menu's badge collection on decrypted copies on a computer (its own README).
- `sound/`: the generators of the tracked interface sounds and the speakers' imitation (its own README).
- `banner/`: the pipeline that makes `app/meta/banner.cgfx` and its checks (its own README).
