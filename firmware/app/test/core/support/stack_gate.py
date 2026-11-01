#!/usr/bin/env python3
"""Stack gate for the pure firmware libraries (05 §5.1, §5.13; review of 2026-10-19).

setup() and loop() run on the 16 KB loop task stack, and at least 4 KB must stay free.
Native tests run on an 8 MB host stack and cannot see an overflow, so env:cardputer-adv
compiles with -fstack-usage and this gate checks the xtensa frames of lib/core, protocol,
link, outbox, input and storage:
  1. every function frame is <= FRAME_MAX bytes, except the capped exceptions below;
  2. every documented worst call chain fits its budget.
It runs after each cardputer-adv link (a violation fails the build) and stand-alone:
  python3 test/core/support/stack_gate.py .pio/build/cardputer-adv
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import NamedTuple

LIBS = ("core", "protocol", "link", "outbox", "input", "storage")
FRAME_MAX = 1536

# Known frames above FRAME_MAX, measured 2026-10-19. Each holds copies of one large
# value (an OutboxCore, a 2 KB line) and runs on its own from loop(), never nested in
# a decode or the boot scan. They may not grow; shrinking them is open work.
FRAME_CAPS = {
    r"\bcpa::outbox_tick\(": 5840,  # two OutboxStep copies (~2.9 KB each)
    r"\bcpa::framer_scan\(": 4224,  # two LineFramer copies (2 KB buffer each)
    r"proto::Codec::encode\(": 4192,  # the 2 KB Line and its Result
    r"\bcpa::link_line\(": 1712,  # LinkStep copies
}

# The deepest json_read path below a per-type decoder (req_text, item_obj and the
# other readers are shallower): req_revs -> req_int -> opt_int -> path_key -> path_key.
_READERS = [
    (r"proto::req_revs\(", 1),
    (r"proto::req_int\(", 1),
    (r"proto::opt_int\(", 1),
    (r"proto::path_key\(", 2),
]

# SdStore's deepest pure path below any FileStore call: map the logical path (the
# physical path is built in a 63-byte FixedStr), then the FatPort call, whose VFS and
# FatFs frames are hal / framework and carried by scripts/stack_budget.py.
_SD_CALL = [
    (r"SdStore::(open_write|read|remove|mkdir|list)\(", 1),
    (r"\bsd_physical\(", 1),
    (r"?\{anonymous\}::(join|short_form)\(", 1),
]

# Worst call chains. A step is (regex, times): it costs the largest matching frame
# times `times`. A step must match some function, so a rename fails loudly; a "?"
# prefix marks a helper that -Os may inline into the step before it. JSON nests at
# most 16 levels (codec.cpp kNestingLimit), so the recursive steps run 17 deep.
CHAINS = {
    "decode queue.snapshot (sent at every handshake)": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"proto::decode_queue\(", 1),
            (r"?\{anonymous\}::fill_rows<", 1),
            (r"?\{anonymous\}::queue_row\(", 1),
            *_READERS,
        ],
    ),
    "decode any message (largest per-type decoder)": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"proto::decode_\w+\(", 1),
            (r"?\{anonymous\}::(fill_rows<|\w+_row\(|\w+_body\()", 1),
            *_READERS,
        ],
    ),
    "decode hello (either role)": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"proto::decode_hello\(", 1),
            (r"?\{anonymous\}::decode_(device|host)_hello\(", 1),
            *_READERS,
        ],
    ),
    "decode debug.state reply (test firmware only)": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"?proto::decode_debug_state\(", 1),
            (r"?\{anonymous\}::debug_summary\(", 1),
            (r"?\{anonymous\}::(debug_view|fill_projects)\(", 1),  # called one after the other
            (r"?\{anonymous\}::debug_project\(", 1),
            *_READERS,
        ],
    ),
    "decode: UTF-8 walk at the nesting limit": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"?\{anonymous\}::(bad_utf8|descend)\(", 1),
            (r"\{anonymous\}::utf8_clean\(", 17),
            (r"proto::utf8_valid\(", 1),
        ],
    ),
    "decode: JSON parse at the nesting limit": (
        3072,
        [
            (r"proto::Codec::decode\(", 1),
            (r"JsonDeserializer<.*>::parseVariant\(", 17),
            (r"JsonDeserializer<.*>::parseQuotedString\(", 1),
        ],
    ),
    "boot scan: check one pending item": (
        4096,
        [
            (r"\boutbox_boot_scan\(", 1),
            (r"?\{anonymous\}::check_item\(", 1),
            (r"proto::Codec::decode\(", 1),
            (r"proto::decode_outbox_item\(", 1),
            (r"?\{anonymous\}::(note|queue)_body\(", 1),
            *_READERS,
        ],
    ),
    "boot scan: list /outbox and /outbox/bad": (
        2048,
        [
            (r"\boutbox_boot_scan\(", 1),
            (r"ScanVisitor::entry\(", 1),
            (r"\bscan_entry\(", 1),
            (r"\boutbox_insert_(bad|pending)\(", 1),
        ],
    ),
    # v0.2 storage (docs/09-m2-design.md §3.5-3.7): the outbox through SdStore.
    "save on SD: outbox_persist -> commit_file -> SdStore": (
        2048,
        [
            (r"\boutbox_persist\(", 1),
            (r"\bcommit_file\(", 1),
            (r"?\{anonymous\}::(fat|atomic)_commit\(", 1),
            *_SD_CALL,
        ],
    ),
    "move to bad/ on SD: outbox_apply -> move_file -> SdStore": (
        2048,
        [
            (r"\boutbox_apply\(", 1),
            (r"?\{anonymous\}::run_action\(", 1),
            (r"\bmove_file\(", 1),
            (r"?\{anonymous\}::fat_commit\(", 1),
            *_SD_CALL,
        ],
    ),
    "boot: outbox_migrate -> commit_file -> SdStore": (
        2048,
        [
            (r"\boutbox_migrate\(", 1),
            (r"?\{anonymous\}::migrate_one\(", 1),
            (r"\bcommit_file\(", 1),
            (r"?\{anonymous\}::(fat|atomic)_commit\(", 1),
            *_SD_CALL,
        ],
    ),
    "send: outbox_read -> SdStore read": (
        2048,
        [(r"\boutbox_read\(", 1), *_SD_CALL],
    ),
    "boot scan: SdStore list of an epoch directory": (
        2048,
        [
            (r"\boutbox_boot_scan\(", 1),
            (r"SdStore::list\(", 1),
            (r"SdStore::list_items\(", 1),
            (r"EpochVisitor::entry\(", 1),
            (r"\bsd_item_name\(", 1),  # returns before the entry is passed on: counted anyway
            (r"ScanVisitor::entry\(", 1),
            (r"\bscan_entry\(", 1),
            (r"\boutbox_insert_(bad|pending)\(", 1),
        ],
    ),
}

_SU_LINE = re.compile(r"^[^:]*:\d+:\d+:(?P<name>.*)\t(?P<size>\d+)\t(?P<qual>[\w,]+)$")


class Frame(NamedTuple):
    name: str
    size: int
    qual: str


def load_frames(build_dir: Path) -> list[Frame]:
    """Frames from <build>/lib*/<lib>/*.su for the pure libraries only."""
    frames = []
    for su in sorted(build_dir.glob("lib*/*/*.su")):
        if su.parent.name not in LIBS:
            continue
        for line in su.read_text().splitlines():
            m = _SU_LINE.match(line)
            if m:
                frames.append(Frame(m["name"], int(m["size"]), m["qual"]))
    return frames


def _matches(frames: list[Frame], pattern: str) -> list[Frame]:
    return [f for f in frames if re.search(pattern, f.name)]


def chain_cost(frames: list[Frame], steps: list[tuple[str, int]]) -> tuple[int, list[str]]:
    """Total bytes of a chain and the required steps that matched nothing."""
    total, missing = 0, []
    for pattern, times in steps:
        optional = pattern.startswith("?")
        hits = _matches(frames, pattern.lstrip("?"))
        if not hits and not optional:
            missing.append(pattern)
        total += times * max((f.size for f in hits), default=0)
    return total, missing


def _frame_limit(name: str, frame_max: int, caps: dict[str, int]) -> int:
    for pattern, cap in caps.items():
        if re.search(pattern, name):
            return cap
    return frame_max


def check(
    frames: list[Frame], frame_max: int = FRAME_MAX, caps: dict | None = None, chains: dict | None = None
) -> list[str]:
    caps = FRAME_CAPS if caps is None else caps
    chains = CHAINS if chains is None else chains
    bad = []
    for f in frames:
        limit = _frame_limit(f.name, frame_max, caps)
        if f.size > limit:
            bad.append(f"frame {f.size} B > {limit} B: {f.name}")
        if "dynamic" in f.qual and "bounded" not in f.qual:
            bad.append(f"unbounded dynamic frame: {f.name}")
    for title, (budget, steps) in chains.items():
        total, missing = chain_cost(frames, steps)
        if missing:
            bad.append(f"chain '{title}': no function matches {', '.join(missing)}")
        if total > budget:
            bad.append(f"chain '{title}': {total} B > {budget} B")
    return bad


def main(argv: list[str], chains: dict | None = None) -> int:
    build_dir = Path(argv[0]) if argv else Path(".pio/build/cardputer-adv")
    frames = load_frames(build_dir)
    if not frames:
        print(f"stack gate: no lib .su files under {build_dir} (build with -fstack-usage)")
        return 1
    chains = CHAINS if chains is None else chains
    for title, (budget, steps) in chains.items():
        print(f"stack gate: {chain_cost(frames, steps)[0]:5d} / {budget} B  {title}")
    bad = check(frames, chains=chains)
    for line in bad:
        print(f"stack gate: FAIL {line}")
    biggest = max(frames, key=lambda f: f.size)
    print(f"stack gate: {len(frames)} frames, largest {biggest.size} B, {len(bad)} violation(s)")
    return 1 if bad else 0


try:  # as a PlatformIO extra script: check after every link of the firmware
    Import("env")  # provided by SCons; a NameError anywhere else
    env.AddPostAction(
        "$BUILD_DIR/${PROGNAME}.elf",
        lambda target, source, env: main([env.subst("$BUILD_DIR")]),
    )
except NameError:  # imported by pytest or run stand-alone
    pass

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
