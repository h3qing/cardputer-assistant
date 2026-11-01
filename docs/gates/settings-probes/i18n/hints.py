"""docs/21's hint bars laid out like theme::hint_items: keycap = text + 4 px (at least 12),
three spacing levels; prints (level, width) per bar. Usage: python3 -I hints.py <efont_cn12.c>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import efont  # noqa: E402

W = efont.Font(sys.argv[1]).width
LEVELS = [(4, 8, 3), (4, 4, 2), (1, 3, 1)]  # (x0, gap, pad)


def cap_w(k):
    return max(W(k) + 4, 12)


def items_w(items, lv):
    x0, gap, pad = lv
    tot = 0
    for i, (k, t) in enumerate(items):
        w = W(t) + ((cap_w(k) + (pad if t else 0)) if k else 0)
        tot += w + (gap if i else 0)
    return x0 + tot


def fit(items):
    for i, lv in enumerate(LEVELS):
        if items_w(items, lv) <= 240:
            return i, items_w(items, lv)
    return None, items_w(items, LEVELS[-1])


BARS = {
    "settings zh": [(";.", "选"), ("Enter", "改"), ("`", "返回"), ("Tab", "菜单")],
    "settings en": [(";.", "pick"), ("Enter", "change"), ("`", "back"), ("Tab", "menu")],
    "device zh": [(",/", "换页"), ("`", "返回"), ("Tab", "菜单")],
    "device en": [(",/", "page"), ("`", "back"), ("Tab", "menu")],
    "wifi zh": [("`", "返回"), ("Tab", "菜单")],
    "wifi en": [("`", "back"), ("Tab", "menu")],
}
for name, items in BARS.items():
    print(name, fit(items))
