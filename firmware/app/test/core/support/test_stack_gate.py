"""Tests for stack_gate.py (run: uv run --with pytest pytest firmware/app/test/core/support -q)."""

from pathlib import Path

import stack_gate as gate


def su(tmp_path: Path, rel: str, text: str) -> None:
    path = tmp_path / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def frame(name: str, size: int, qual: str = "static") -> gate.Frame:
    return gate.Frame(name, size, qual)


def test_parses_names_with_colons_and_reads_only_the_pure_libs(tmp_path: Path) -> None:
    su(
        tmp_path,
        "lib533/protocol/codec.cpp.su",
        "codec.cpp:163:9:cpa::proto::Codec::decode(const char*, size_t)\t640\tstatic\n",
    )
    su(tmp_path, "lib22c/outbox/outbox_io.cpp.su", "outbox_io.cpp:9:1:cpa::f()\t32\tdynamic,bounded\n")
    su(tmp_path, "lib256/M5Unified/M5Unified.cpp.su", "M5Unified.cpp:1:1:m5::big()\t9000\tstatic\n")
    su(tmp_path, "FrameworkArduino/main.cpp.su", "main.cpp:1:1:loopTask(void*)\t9000\tstatic\n")
    su(tmp_path, "lib533/protocol/sub/other.cpp.su", "other.cpp:1:1:x()\t9000\tstatic\n")
    frames = gate.load_frames(tmp_path)
    assert sorted(frames) == [
        frame("cpa::f()", 32, "dynamic,bounded"),
        frame("cpa::proto::Codec::decode(const char*, size_t)", 640),
    ]


def test_frames_over_the_limit_fail_unless_capped() -> None:
    frames = [frame("cpa::a()", 1536), frame("cpa::b()", 1537), frame("cpa::tick(int)", 5000)]
    bad = gate.check(frames, frame_max=1536, caps={r"\btick\(": 5000}, chains={})
    assert len(bad) == 1 and "cpa::b()" in bad[0] and "1537" in bad[0]
    bad = gate.check(frames + [frame("cpa::tick(long)", 5001)], frame_max=1536, caps={r"\btick\(": 5000}, chains={})
    assert len(bad) == 2 and any("cpa::tick(long)" in b and "5000" in b for b in bad)


def test_unbounded_dynamic_frames_fail() -> None:
    frames = [frame("cpa::vla(int)", 16, "dynamic"), frame("cpa::ok()", 16, "dynamic,bounded")]
    bad = gate.check(frames, frame_max=1536, caps={}, chains={})
    assert len(bad) == 1 and "cpa::vla(int)" in bad[0]


def test_chain_cost_takes_the_largest_match_times_its_count() -> None:
    frames = [
        frame("cpa::decode(a)", 400),
        frame("cpa::decode_queue(b)", 900),
        frame("cpa::decode_ping(b)", 100),
        frame("cpa::walk(c)", 50),
    ]
    steps = [(r"::decode\(", 1), (r"::decode_\w+\(", 1), (r"::walk\(", 17)]
    assert gate.chain_cost(frames, steps) == (400 + 900 + 17 * 50, [])
    ok = gate.check(frames, frame_max=1536, caps={}, chains={"c": (2150, steps)})
    assert ok == []
    over = gate.check(frames, frame_max=1536, caps={}, chains={"c": (2149, steps)})
    assert len(over) == 1 and "2150" in over[0]


def test_a_chain_step_that_matches_nothing_fails_unless_optional() -> None:
    bad = gate.check([frame("cpa::a()", 8)], frame_max=1536, caps={}, chains={"c": (99, [(r"::gone\(", 1)])})
    assert len(bad) == 1 and "::gone" in bad[0]
    # "?" marks a helper that -Os may inline: its frame is then part of its caller's
    steps = [(r"::a\(", 1), (r"?::inlined\(", 3)]
    assert gate.chain_cost([frame("cpa::a()", 8)], steps) == (8, [])
    assert gate.check([frame("cpa::a()", 8)], frame_max=1536, caps={}, chains={"c": (8, steps)}) == []


def test_main_exit_codes(tmp_path: Path) -> None:
    su(tmp_path, "lib1/core/a.cpp.su", "a.cpp:1:1:cpa::a()\t8\tstatic\n")
    assert gate.main([str(tmp_path)], chains={}) == 0
    su(tmp_path, "lib1/core/b.cpp.su", f"b.cpp:1:1:cpa::b()\t{gate.FRAME_MAX + 1}\tstatic\n")
    assert gate.main([str(tmp_path)], chains={}) == 1
    assert gate.main([str(tmp_path / "missing")], chains={}) == 1


def test_storage_is_gated_with_the_sd_chains(tmp_path: Path) -> None:
    su(
        tmp_path,
        "lib704/storage/sd_store.cpp.su",
        "sd_store.cpp:85:6:virtual bool cpa::SdStore::open_write(const char*)\t112\tstatic\n",
    )
    assert gate.load_frames(tmp_path) == [frame("virtual bool cpa::SdStore::open_write(const char*)", 112)]
    titles = " | ".join(gate.CHAINS)
    for name in ("commit_file", "move_file", "outbox_migrate", "SdStore read", "SdStore list"):
        assert name in titles, name


def test_sd_chains_add_up_through_the_store(tmp_path: Path) -> None:
    frames = [
        frame("bool cpa::outbox_persist(cpa::FileStore&, const cpa::MonoId&, const char*, size_t)", 128),
        frame("bool cpa::commit_file(cpa::FileStore&, const char*, const char*, const char*, size_t)", 32),
        frame("virtual bool cpa::SdStore::open_write(const char*)", 112),
        frame("cpa::Opt<cpa::FixedStr<63> > cpa::sd_physical(const char*, const char*)", 64),
        frame("cpa::Opt<cpa::FixedStr<63> > cpa::{anonymous}::join(const char*, const char*, const char*)", 256),
    ]
    budget, steps = gate.CHAINS["save on SD: outbox_persist -> commit_file -> SdStore"]
    assert gate.chain_cost(frames, steps) == (128 + 32 + 112 + 64 + 256, [])
    assert budget >= 128 + 32 + 112 + 64 + 256
