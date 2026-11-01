"""Reading the daily notes files cpad writes (05 §7.8)."""

from harness.notes import NoteLine, count_block, count_text, parse_line, read_notes


def test_parse_line_with_tag_and_wall_clock():
    line = "- 20:41 #cpa hil capture k3x9 ^cpa-3qzk1m7-0000c-0003"
    assert parse_line(line) == NoteLine("20:41", "cpa", "hil capture k3x9", "3QZK1M7-0000C-0003")


def test_parse_line_without_tag_and_received_time():
    assert parse_line("- ~09:05 just text ^cpa-aaaaaaa-bbbbb-cccc") == NoteLine(
        "~09:05", None, "just text", "AAAAAAA-BBBBB-CCCC"
    )


def test_parse_line_ignores_other_markdown():
    assert parse_line("# heading") is None
    assert parse_line("- 10:00 no block id") is None
    assert parse_line("") is None


def test_read_notes_reads_every_daily_file_in_order(tmp_path):
    (tmp_path / "2026-10-19.md").write_text("- 23:59 a ^cpa-aaaaaaa-bbbbb-0001\n", encoding="utf-8")
    (tmp_path / "2026-10-20.md").write_text(
        "- 00:01 #cpa b ^cpa-aaaaaaa-bbbbb-0002\nstray\n- 00:02 c ^cpa-aaaaaaa-bbbbb-0003\n", encoding="utf-8"
    )
    (tmp_path / "ignore.txt").write_text("- 00:01 x ^cpa-aaaaaaa-bbbbb-0009\n", encoding="utf-8")
    notes = read_notes(tmp_path)
    assert [n.text for n in notes] == ["a", "b", "c"]


def test_read_notes_of_a_missing_dir_is_empty(tmp_path):
    assert read_notes(tmp_path / "nope") == ()


def test_counts_by_text_and_block():
    notes = (
        NoteLine("10:00", None, "dup me", "AAAAAAA-BBBBB-0001"),
        NoteLine("10:01", None, "dup me", "AAAAAAA-BBBBB-0002"),
        NoteLine("10:02", "cpa", "other", "AAAAAAA-BBBBB-0003"),
    )
    assert count_text(notes, "dup me") == 2
    assert count_text(notes, "nothing") == 0
    assert count_block(notes, "aaaaaaa-bbbbb-0003") == 1
