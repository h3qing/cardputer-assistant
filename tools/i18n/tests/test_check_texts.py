"""check_texts: Chinese only in constexpr Txt constants (docs/21 §9.3)."""

from pathlib import Path

import check_texts
from cppscan import has_han, literals

FIRMWARE = Path(__file__).resolve().parents[3] / "firmware" / "app"


def test_literals_skip_comments_and_keep_lines():
    src = '// "中文" in a comment\nconst char* a = "x"; /* "注" */\nint b = f("y\\"z");\n'
    found = literals(src)
    assert [(lit.text, lit.line) for lit in found] == [("x", 2), ('y\\"z', 3)]
    assert "const char* a" in found[0].statement


def test_han_raw_or_escaped():
    assert has_han("队列为空")
    assert has_han("\\xE9\\x80\\x80")
    assert has_han("开头：")
    assert not has_han("Queue · empty ● … ★")


def test_constants_pass():
    src = (
        '#include "txt.h"\nnamespace cpa {\n'
        'constexpr LineTxt kQueueEmpty{"队列为空", "Queue is empty"};\n'
        'constexpr Txt kWeekdays[] = {{"周日", "Sun"},\n {"周一", "Mon"}};\n'
        'constexpr Row kRows[] = {{Action::Home, MenuTxt{"主页", "Home"}}};\n'
        'inline constexpr HelpRow kTiles[] = {{k, "P", "", &txt::kProjects}};\n'
        "}\n"
    )
    assert check_texts.problems_in("lib/x.cpp", src) == []


def test_a_bare_literal_fails():
    src = 'constexpr Txt kA{"甲", "a"};\nconst char* f() {\n  return "队列为空";\n}\n'
    found = check_texts.problems_in("lib/x.cpp", src)
    assert len(found) == 1 and "lib/x.cpp:3" in found[0]


def test_a_constexpr_table_without_txt_fails():
    src = 'constexpr const char* kDays[] = {"周日", "周一"};\n'
    assert len(check_texts.problems_in("modules/home/x.cpp", src)) == 2


def test_an_inline_txt_fails():
    src = 'void f() {\n  draw(tr(Txt{"队列为空", "Queue is empty"}));\n}\n'
    found = check_texts.problems_in("lib/x.cpp", src)
    assert any("inline" in f for f in found)


def test_allowed_files():
    assert check_texts.problems_in("lib/i18n/lang_names.h", 'constexpr const char* k[] = {"中文", "English"};') == []


def test_the_firmware_is_clean():
    assert check_texts.check(FIRMWARE) == []
