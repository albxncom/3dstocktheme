"""make_out.py PIC_D PIC_B OUT SOURCE: the flows' pictures from the true-size ones pics.py drew of the Designs walk
(PIC_D) and the Badges walk (PIC_B): OUT/<band>/<row>-<order>-<name>.png (800 x 960, enlarged twice, nearest
neighbour), one per state of states.py, with OUT/list.json and OUT/list.txt. SOURCE says what was rendered."""
import json
import pathlib
import sys

from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from states import ROWS, STATES  # noqa: E402


def main():
    src = {"designs": pathlib.Path(sys.argv[1]), "badges": pathlib.Path(sys.argv[2])}
    out, source = pathlib.Path(sys.argv[3]), sys.argv[4]
    items, order = [], {}
    for band, row, cap, name, label, line in STATES:
        k = (band, row)
        order[k] = order.get(k, 0) + 1
        im = Image.open(src[band] / (cap + ".png")).convert("RGB")
        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        rel = "%s/%s-%02d-%s.png" % (band, row, order[k], name)
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        im.save(out / rel, optimize=True)
        size = (out / rel).stat().st_size
        items.append({"band": band, "row": row, "row_title": ROWS[k], "order": order[k], "label": label,
                      "file": rel, "width": im.width, "height": im.height, "bytes": size, "look": "Stock",
                      "scheme": "Weiss", "language": "Deutsch", "capture": cap, "line": line})
    data = {"source": source + ", demonstration build on the host renderer, stand-in fixtures only",
            "picture": "top screen 400 x 240 above the bottom screen 320 x 240 (centred, x 40), true size enlarged 2x, "
                       "nearest", "states": items}
    (out / "list.json").write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    txt = [data["source"], data["picture"]]
    last = None
    for it in items:
        if (it["band"], it["row"]) != last:
            last = (it["band"], it["row"])
            n = sum(1 for x in items if (x["band"], x["row"]) == last)
            txt += ["", "%s / %s: %s (%d)" % (it["band"], it["row"], it["row_title"], n)]
        txt.append("%02d  %s  [%s, %d x %d, %d bytes]  %s" % (it["order"], it["label"], it["file"], it["width"],
                                                          it["height"], it["bytes"], it["line"]))
    (out / "list.txt").write_text("\n".join(txt) + "\n", encoding="utf-8")
    print(len(items), "pictures in", out)


main()
