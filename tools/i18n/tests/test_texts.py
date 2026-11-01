"""texts: the pairs, measured (docs/21 §9.3)."""

from pathlib import Path

import texts

FIRMWARE = Path(__file__).resolve().parents[3] / "firmware" / "app"


def test_width_table():
    assert texts.width("Queue") == 30
    assert texts.width("队列") == 24
    assert texts.width("● … ·") == 30
    assert texts.width("√") == 12


def test_pairs_of_constants_and_arrays():
    src = (
        'constexpr MenuTxt kUp{"上移", "Up"};\n'
        'constexpr Txt kDays[] = {{"周日", "Sun"}, {"周一", "Mon"}};\n'
        'constexpr Txt kAgo{"%s前", "%s ago"};\n'
        'constexpr Txt kMark{"\\xE2\\x88\\x9A 已存", "\\xE2\\x88\\x9A saved"};\n'
    )
    rows = texts.pairs("lib/x.cpp", src)
    assert [(r["name"], r["zh"], r["en"]) for r in rows] == [
        ("kUp", "上移", "Up"),
        ("kDays", "周日", "Sun"),
        ("kDays", "周一", "Mon"),
        ("kAgo", "%s前", "%s ago"),
        ("kMark", "√ 已存", "√ saved"),
    ]
    assert rows[0]["limit"] == 120 and rows[3]["limit"] is None
    table = texts.pairs("lib/x.cpp", 'constexpr Row k[] = {{Action::Home, MenuTxt{"主页", "Home"}}};\n')
    assert table[0]["kind"] == "MenuTxt" and table[0]["name"] == "k"


def test_the_firmware_report():
    rows = texts.report(FIRMWARE)
    assert len(rows) > 300
    assert not [r for r in rows if r["over"]]
    assert all(r["en"] and r["zh"] for r in rows)
    assert "file\tconstant" in texts.tsv(rows)
