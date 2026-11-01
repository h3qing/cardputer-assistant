"""Measure every CJK literal and its English draft with efontCN_12 (simulator metrics).

Usage: python3 -I measure.py <efont_cn12.c> <lits2.json> <out.json>
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import efont
from en_draft import T

F = efont.Font(sys.argv[1])
rows = json.load(open(sys.argv[2]))
W = F.width
LEVELS = [(4, 8, 3), (4, 4, 2), (1, 3, 1)]
HAN_KEYS = ("退格", "空格")


def cap_w(key):
    return max(W(key) + 4, 12)


def zh_items(h):
    """Port of theme::hint_items (docs/19 §4.2 rules 1-5): [(key, text)]."""
    items, open_ = [], False
    for word in h.split():
        n = 0
        while n < len(word) and "!" <= word[n] <= "~":
            n += 1
        if n == len(word) and open_:
            k, t = items[-1]; items[-1] = (k + " " + word, t); continue
        if n > 0:
            items.append((word[:n], word[n:])); open_ = word[n:] == ""; continue
        hk = next((k for k in HAN_KEYS if word.startswith(k)), None)
        if hk:
            items.append((hk, word[len(hk):])); open_ = word[len(hk):] == ""; continue
        if open_:
            k, _ = items[-1]; items[-1] = (k, word); open_ = False; continue
        items.append(("", word)); open_ = False
    return items


def items_w(items, lv):
    x0, gap, pad = lv
    tot = 0
    for i, (k, t) in enumerate(items):
        w = W(t)
        if k:
            w += cap_w(k) + (pad if t else 0)
        tot += w + (gap if i else 0)
    return x0 + tot


def hint_fit(items):
    for i, lv in enumerate(LEVELS):
        if items_w(items, lv) <= 240:
            return i, items_w(items, lv)
    return None, items_w(items, LEVELS[-1])


BUDGET = {"toast": 224, "line": 232, "help": 120, "tile": 53, "sign": 36, "title": 232, "day": 24,
          "keys": 224, "sysrow": 232}
out = []
seen = set()
for r in rows:
    if r["cls"] != "cjk":
        continue
    stem = os.path.splitext(os.path.basename(r["file"]))[0]
    zh = r["text"].replace("\\xE2\\x97\\x8F", "●")
    if (stem, zh) in seen:
        continue
    seen.add((stem, zh))
    en, cat = T[(stem, zh)]
    rec = {"file": r["file"], "line": r["line"], "zh": zh, "cat": cat}
    if cat in ("hint", "keys"):
        zi = zh_items(zh)
        rec["en"] = "  ".join(f"[{k}] {t}" for k, t in en)
        if cat == "hint":
            rec["zh_lv"], rec["zh_w"] = hint_fit(zi)
            rec["en_lv"], rec["en_w"] = hint_fit(en)
            rec["en_naive"] = " ".join(k + t for k, t in en)  # what rule 2 would do with "Enteropen"
        else:
            rec["zh_w"], rec["en_w"] = items_w(zi, LEVELS[0]), items_w(en, LEVELS[0])
        rec["over"] = (rec.get("en_lv", 0) is None) or rec["en_w"] > (240 if cat == "hint" else 224)
    else:
        rec["en"] = en
        z = zh.replace("%u", "12").replace("%s", "").replace("%lu", "12").replace("%d%%", "83%")
        e = en.replace("%u", "12").replace("%s", "").replace("%lu", "12").replace("%d%%", "83%")
        rec["zh_w"], rec["en_w"] = W(z), W(e)
        b = BUDGET.get(cat)
        rec["budget"] = b
        rec["over"] = b is not None and rec["en_w"] > b
    out.append(rec)
json.dump(out, open(sys.argv[3], "w"), ensure_ascii=False, indent=1)
