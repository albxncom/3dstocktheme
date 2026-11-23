/* The installing screen (R9, the tile d "Nachbau"; G2-L, the layout C "Zwei Teile"), in top screen px: one small head
 * line; under it the line with the holders over the whole screen; a glass tile seen from the front on a wide stage at
 * the left with the picture set into its face and the name under it; the steps a column without a box at the right;
 * at the end a line under it all. Nothing stands on a card. Every look draws it (pour_ui.h, pour_art.py); the model
 * (pour.h) knows only the line and where the pieces fall (POUR_MOUTH_X, POUR_FALL). art.py reads these with layout.h. */
#ifndef POUR_LAYOUT_H
#define POUR_LAYOUT_H

/* FB-L (C104, "kachel und die punkte rechts inkl leine um 4 px weiter nach oben"): the line, its holders and what
 * hangs from them, the tile with all in it and the steps stand 4 px higher than G2-L's places, every time the same;
 * the name 3 px further from the tile, the end's line 4 px further from the screen's lower edge. C110-L ("noch 2 px
 * mehr zwischen kachel und text"): the name 2 px lower again, nothing else moves. C146 ("nur eine zeile, wenn nötig
 * truncating mit ..."): the name is one line, so the end's line stands where it stood before C110-F moved it. */

#define POUR_HEAD_Y 4               /* the head's line box: its symbol, POUR_HEAD_GAP after it its text, from the */
#define POUR_HEAD_LH 16             /* left edge (TOP_PAD) */
#define POUR_HEAD_PX 12
#define POUR_HEAD_ICON 12
#define POUR_HEAD_GAP 5
#define POUR_HEAD_DOTS_Y 23         /* the rule under it */
#define POUR_LINE_Y 48              /* the line: 2 px from here, over the whole screen */
#define POUR_KNOT 5                 /* the knot on it over the pieces' way */
#define POUR_TILE_X 86              /* the tile: nearly square, rounded, its depth by light and shade alone, closed */
#define POUR_TILE_Y 86              /* a hanging piece's tip 14 px over its upper edge (C51) */
#define POUR_TILE_W 84
#define POUR_TILE_H 82
#define POUR_TILE_R 19
#define POUR_RIM 2                  /* its glass edge: the inner shape (where the liquid stands) is this far in */
#define POUR_INNER_X (POUR_TILE_X + POUR_RIM)
#define POUR_INNER_Y (POUR_TILE_Y + POUR_RIM)
#define POUR_INNER_W (POUR_TILE_W - 2 * POUR_RIM)
#define POUR_INNER_H (POUR_TILE_H - 2 * POUR_RIM)
#define POUR_INNER_R (POUR_TILE_R - POUR_RIM)
/* Prisma's tile (C48, the canvas's Prisma a "geschnittene Ecken"): cut crystal in the same box, its corners cut
 * POUR_PRISM_CUT px along each edge; the inner shape lies POUR_RIM px inside that outline, its corners cut too. */
#define POUR_PRISM_CUT 11
#define POUR_HOLDER_W 8             /* a holder (C24, the canvas's B "Zange"): its clamp on the line, which does not */
#define POUR_HOLDER_H 6             /* swing, */
#define POUR_HOLDER_Y 45
#define POUR_JOINT_Y 51             /* the joint under it, 2 px under the line's middle: what hangs turns about it, */
#define POUR_TONGS_W 16             /* the tongs from it to the piece in their poses (pour.h POUR_POSES) side by side, */
#define POUR_TONGS_H 14             /* each cell's top on the line's middle, the holder's x across its middle, a clear */
#define POUR_TONGS_Y 49             /* pixel round them */
#define POUR_PIECE_Y 64             /* a piece (the canvas's glass octahedron b): its middle this far down, its foot */
#define POUR_PIECE_FOOT 8           /* this far under it; its sprite POUR_PIECE_W x POUR_PIECE_H with the middle 1 px */
#define POUR_PIECE_W 16             /* over the sprite's (room for the hanging piece's shadow under it) */
#define POUR_PIECE_H 22
#define POUR_LEAVE_MS 100          /* a piece let go comes upright from its holder's pose in this time (pour_ui.h) */
#define POUR_MELT_W 40              /* a landing (the canvas's impact z2): POUR_MELT_N frames at POUR_MELT_FPS, each */
#define POUR_MELT_H 11              /* this size, the ground under its last row, the place of the touch between */
#define POUR_MELT_N 15              /* its two middle columns; drawn in the inner shape only */
#define POUR_MELT_FPS 30
#define POUR_MELT_COLS 8            /* the frames in rows of this many (all 15 side by side pass the atlas's edge) */
#define POUR_CURVE 4                /* the surface at rest: one calm curve, at the walls this far over its lowest point */
#define POUR_UNDER 11               /* under the surface the deeper tone fades out over this many rows */
#define POUR_PICTURE 44             /* the picture in the middle of the face in its frame's line (R19), in front of */
#define POUR_PICTURE_X 106          /* the liquid and of the pieces, which fall through the tile's middle behind it */
#define POUR_PICTURE_Y 105
#define POUR_NAME_Y 178             /* the name under the tile, centred under it: one line of POUR_NAME_W, cut at */
#define POUR_NAME_LH 16             /* its end with "..." */
#define POUR_NAME_PX 14
#define POUR_NAME_W 232
#define POUR_AROUND 16              /* the tile's glow, shadow and light on the ground reach at most this far round it */
#define POUR_LIST_X 252             /* the steps beside the stage, a column without a box: a row per step, in it the */
#define POUR_LIST_Y 82              /* step's mark and POUR_MARK_GAP after it its name; the first row's top, clear of */
                                    /* the hanging pieces (their shadow ends on row 75) */
#define POUR_ROW 22                 /* four steps */
#define POUR_ROW_PX 12
#define POUR_MARK 13                /* a step's mark: a ring, with a dot while under way, the check once done */
#define POUR_MARK_GAP 8
#define POUR_END_Y 208              /* the end's line box (C53): a check, POUR_END_GAP after it the sentence, centred */
#define POUR_END_LH 18
#define POUR_END_PX 14
#define POUR_END_TICK 12
#define POUR_END_GAP 6

#endif
