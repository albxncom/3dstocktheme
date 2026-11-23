"""3DStockTheme's half of the image generator: the hook the kit's art.py describes at APP (art.py --app app_art.py).
The images are drawn in app_images.py (as 0.9.3 draws them, with the kit's helpers) and app_look_<look>.py (in each
look's material); this module only hands them over, and imports them when the kit asks, as they import the kit's
helpers from art, which is whole by then.

Its own commands (main):
  python3 -I art.py --app app_art.py [--look NAME] --sheet OUT.png
                                     every image, twice the size, on the background colour, to look at
  python3 -I art.py --app app_art.py --compare OUT.png NORMAL.bmp HELD.bmp [BAR_HELD.bmp]
                                     the start screen's bottom screen from these images next to console
                                     screenshots (24-bit BMP, 320x240), three times the size: plain, a tile held,
                                     the bar held; the scroll bar's thumb at rest and held
"""
from pathlib import Path

LAYOUT = Path(__file__).resolve().parent / "layout.h"   # includes the kit's ui_layout.h, and pour_layout.h
# the names only Stock fills: the glass's Clear and Platin slots and 0.9.3's step marks
STOCK_ONLY = frozenset(
    ["DESIGNS", "DESIGNS_PLATIN", "BADGES", "BADGES_PLATIN", "MUSIC_BADGE", "MUSIC_BADGE_PLATIN", "DONE_BADGE",
     "DONE_BADGE_PLATIN"]
    + ["STEP_DROP_LATER", "STEP_NOW_GLASS_SHARD", "STEP_DONE_GLASS_SHARD", "STEP_TUBE", "STEP_TUBE_FILL", "STEP_ROD",
       "STEP_ROD_FILL"]
    + ["STEP_%s_%s" % (k, s) for k in ("CHAMBER", "SETTING") for s in ("LATER", "NOW", "DONE", "CORE")])


def sprites(index):
    """The application's sprites of the atlas in scheme index, in the look drawn."""
    import app_images
    items = app_images.sprites()
    return app_images.LOOK.sprites(items, index) if app_images.LOOK else items


def sheet():
    """The application's sprites of the sheet in the scheme drawn (art.SCHEME), in the look drawn."""
    import app_images
    if not hasattr(app_images.LOOK, "sheet"):
        return app_images.sheet_sprites("shard")
    return app_images.LOOK.sheet([s.name for s in app_images.sheet_sprites("clear")])


def pictures(index):
    """The part's pictures in scheme index (the one drawn): the start screen's turning house, LOGO_SHEET px square,
    in the look drawn (its logo hook), else 0.9.3's in Shard."""
    import app_images
    if hasattr(app_images.LOOK, "logo"):
        return app_images.LOOK.logo(app_images.Turner())
    return app_images.logo_sheet()


def main(args):
    """The commands above; True if args are one of them."""
    import app_images
    if len(args) == 2 and args[0] == "--sheet":
        app_images.sheet(args[1])
        return True
    if len(args) in (4, 5) and args[0] == "--compare":
        app_images.compare(*args[1:])
        return True
    return False
