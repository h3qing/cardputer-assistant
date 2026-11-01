#!/usr/bin/env python3
"""Stack budget of the application layers (05 §5.1, §5.13; WP2 handover).

v0.3 (docs/11 §4.7): the loop also parses file.* requests, encodes file.data and
file.items and drives the recording state machine; those chains are below. The audio
task has its own 6 KB stack, measured on the device (device/test_rec_budget).

The loop task has 16 KB and at least 4 KB must stay free, so every documented call
chain from setup() / loop() down to the deepest frame must fit in 12 KB. The pure
libraries are gated by test/core/support/stack_gate.py; this script adds the layers on
top (src/, shell/, module_api/, modules/, hal/, usb_transport/) and the chains through
them, reusing the pure libraries' chain steps. Framework and M5 library frames are not
in the .su files we read: each chain carries a fixed allowance for them.

v0.2 (docs/09-m2-design.md §5, R20): a chain through SdStore also carries FATFS bytes
for the VFS, FatFs (FF_USE_LFN=2 keeps its ~512 B name buffer on the stack) and SPI
driver frames below hal's VfsFatPort, none of which are in our .su files.

Runs after every cardputer-adv link (a violation fails the build) and stand-alone:
  python3 scripts/stack_budget.py .pio/build/cardputer-adv

Device suites (pio test -e cardputer-adv-test) get one check instead: their own frames
follow APP_FRAME_MAX too. Most run on Arduino's default 8 KB loop stack, where a
6,256 B frame in test_rec_sd (a 4 KB RecIndex temporary) left too little for the SD
card's SPI chain below it (docs/07 §6.7 #1).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    _APP_DIR = Path(__file__).resolve().parents[1]
except NameError:  # executed by SCons as a PlatformIO extra script: no __file__
    Import("env")  # noqa: F821
    _APP_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
sys.path.insert(0, str(_APP_DIR / "test" / "core" / "support"))
import stack_gate as gate

# hal includes v0.2's sd_card (VfsFatPort, hal_sd_mount) and part_facts; rec is v0.3's
# recording library (RecCore and file_req run on the loop, the rest on the audio task).
# v0.4.0 (docs/13 §10.1): help is the Tab menu library, home the launcher module.
# v0.5 (docs/19 §3): theme draws every page's shapes.
APP_DIRS = ("shell", "module_api", "hal", "usb_transport", "rec", "board", "inbox", "system", "help", "home", "theme")
APP_FRAME_MAX = 4096
LOOP_BUDGET = 12 * 1024  # 16 KB loop stack - 4 KB that must stay free
FRAMEWORK = 1024  # loopTask, Arduino and newlib frames under setup()/loop()
FATFS = 2048  # VFS + FatFs + SD driver frames below VfsFatPort (09 §5)
FATFS_STEP = (r"?__fatfs_allowance__", 1)

_LOOP = [(r"^void loop\(\)", 1), (r"Runtime::loop\(", 1)]
_RX_LINE = [*_LOOP, (r"Runtime::pump_rx\(", 1), (r"Runtime::handle_line\(", 1)]
_DECODE = [s for s in gate.CHAINS["decode any message (largest per-type decoder)"][1]]
_ENCODE = [(r"proto::Codec::encode\(", 1), (r"?encode_payload\(", 1)]
_SAVE = [(r"Runtime::save_item\(", 1), (r"Runtime::encode_persist\(", 1), *_ENCODE]
_PERSIST = [
    (r"Runtime::save_item\(", 1),
    (r"Runtime::encode_persist\(", 1),
    (r"\boutbox_persist\(", 1),
    (r"?LfsStore::(write|close|rename)\(", 1),
]
_TRACK = [
    (r"Runtime::save_item\(", 1),
    (r"Runtime::track_added\(", 1),
    (r"\boutbox_added\(", 1),
]
# Below any FileStore call on the SD card: SdStore (pure, its path mapping included),
# hal's VfsFatPort, then the framework's VFS / FatFs / SPI frames (FATFS).
_SD = [
    *gate.CHAINS["send: outbox_read -> SdStore read"][1][1:],
    (r"VfsFatPort::(open_write|write|sync_close|read|unlink|mkdir|list)\(", 1),
    FATFS_STEP,
]
_SD_COMMIT = [(r"\bcommit_file\(", 1), (r"?\{anonymous\}::(fat|atomic)_commit\(", 1), *_SD]
_ACK = [
    *_RX_LINE,
    (r"?Runtime::link_effect\(", 1),
    (r"?Runtime::deliver\(", 1),
    (r"Runtime::deliver_reply\(", 1),
    (r"Runtime::track_ack\(", 1),
]
_KEY = [*_LOOP, (r"Runtime::pump_keys\(", 1), (r"?Runtime::key_event\(", 1)]
_RENDER = [*_LOOP, (r"Runtime::render\(", 1), (r"?Runtime::render_full\(", 1), (r"Shell::render\(", 1)]
# v0.5 S3 (docs/19 §3.5): between full frames, an animation tick or a slide frame
_ANIMATE = [*_LOOP, (r"Runtime::render\(", 1), (r"?Runtime::animate\(", 1), (r"Shell::animate\(", 1)]
_TEXT = [(r"\bdraw_fit\(", 1), (r"\bfit_text\(", 1), (r"M5Display::(text|textWidth|walk)\(", 2)]
# v0.5 (docs/19 §7): Mando, his bubble or sign, down to the 4-bit blit or a rounded fill
_MANDO = [
    (r"theme::(mando|sign|pill|live_capsule|card)\(", 1),
    (r"?theme::bubble\(", 1),
    (r"RasterCanvas::(art|digits|dot|fillRound)\(", 1),
    (r"theme::(blit4|dot|fill_round)\(", 1),
    (r"M5Display::(text|textWidth|walk)\(", 2),
]
# v0.4.0 (docs/13 §6): the menu's 1.6 KB MenuContent is built in menu_step / menu_draw,
# kept out of line so it is never next to another large frame; below it the deepest
# row builder (a binding's key label) and the row drawing.
_MENU_ROWS = [
    (r"\bmenu_content\(", 1),
    (r"?\{anonymous\}::add_(here|goto|control)\(", 1),
    (r"?\{anonymous\}::(add_layer<|add_lock_rows\()", 1),
    (r"?\{anonymous\}::binding_row\(", 1),
    (r"\bkey_(range_)?label\(", 1),
]
_MIGRATE = [
    (r"^void setup\(\)", 1),
    (r"Runtime::begin\(", 1),
    (r"?Runtime::begin_storage\(", 1),
    (r"Runtime::migrate_boot\(", 1),
    (r"\boutbox_migrate\(", 1),
    (r"?\{anonymous\}::migrate_one\(", 1),
]


def chain(*steps):
    return [*steps, (r"?__framework_allowance__", 1)]


CHAINS = {
    "loop: inbound line, decode": (LOOP_BUDGET, [*_RX_LINE, *_DECODE]),
    "loop: inbound line, reply encoded": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"?Runtime::link_effect\(", 1),
            (r"Runtime::send_reply\(", 1),
            (r"Runtime::transmit\(", 1),
            *_ENCODE,
        ],
    ),
    "loop: framer step": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_rx\(", 1),
            (r"?Runtime::scan_once\(", 1),
            (r"\bframer_scan\(", 1),
        ],
    ),
    "loop: snapshot delivered (sessions, toast)": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"?Runtime::link_effect\(", 1),
            (r"?Runtime::deliver\(", 1),
            (r"Runtime::deliver_snapshot\(", 1),
            (r"\breduce_sessions\(", 1),
            (r"?Shell::toast\(", 1),
            (r"\btoast_push\(", 1),
        ],
    ),
    "loop: agent result delivered": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"?Runtime::link_effect\(", 1),
            (r"?Runtime::deliver\(", 1),
            (r"Runtime::deliver_reply\(", 1),
            (r"Runtime::agent_events\(", 1),
            (r"Runtime::agent_event\(", 1),
            (r"?Shell::toast\(", 1),
        ],
    ),
    "loop: outbox.ack delivered": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"?Runtime::link_effect\(", 1),
            (r"?Runtime::deliver\(", 1),
            (r"Runtime::deliver_reply\(", 1),
            (r"Runtime::track_ack\(", 1),
            (r"\boutbox_acked\(", 1),
        ],
    ),
    "loop: failed agent request saved as queue item": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::flush_agent_save\(", 1),
            (r"?Runtime::save_queue\(", 1),
            *_SAVE,
        ],
    ),
    "loop: item tracked after saving": (
        LOOP_BUDGET,
        [*_LOOP, (r"Runtime::flush_agent_save\(", 1), *_TRACK],
    ),
    "loop: link lost (outbox, agent)": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_timers\(", 1),
            (r"Runtime::link_changed\(", 1),
            (r"Runtime::track_link_lost\(", 1),
            (r"\boutbox_link_lost\(", 1),
            (r"?Runtime::apply_outbox\(", 1),
        ],
    ),
    "loop: key saves a note (encode)": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_keys\(", 1),
            (r"?Runtime::key_event\(", 1),
            (r"Shell::on_key\(", 1),
            (r"?Shell::compose_key_event\(", 1),
            (r"Shell::submit_compose\(", 1),
            (r"?Runtime::save_note\(", 1),
            *_SAVE,
        ],
    ),
    "loop: key saves a note (persist)": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_keys\(", 1),
            (r"?Runtime::key_event\(", 1),
            (r"Shell::on_key\(", 1),
            (r"?Shell::compose_key_event\(", 1),
            (r"Shell::submit_compose\(", 1),
            (r"?Runtime::save_note\(", 1),
            *_PERSIST,
        ],
    ),
    "loop: confirm sends agent.spawn": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_keys\(", 1),
            (r"Shell::on_key\(", 1),
            (r"?Shell::confirm_key_event\(", 1),
            (r"?Shell::accept_confirm\(", 1),
            (r"Shell::execute\(", 1),
            (r"Runtime::send_agent\(", 1),
            (r"Runtime::agent_events\(", 1),
            (r"Runtime::agent_event\(", 1),
            (r"Runtime::transmit\(", 1),
            *_ENCODE,
        ],
    ),
    "loop: outbox tick": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_outbox\(", 1),
            (r"?Runtime::tick_outbox\(", 1),
            (r"\boutbox_tick\(", 1),
        ],
    ),
    "loop: render (board home)": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::render\(", 1),
            (r"Shell::render\(", 1),
            (r"BoardModule::render(_home|_project)?\(", 1),
            (r"?project_row\(", 1),
            (r"\bdraw_fit\(", 1),
            (r"\bfit_text\(", 1),
            (r"M5Display::(text|textWidth|walk)\(", 2),
        ],
    ),
    "loop: render (compose, system)": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::render\(", 1),
            (r"Shell::render\(", 1),
            (
                r"(Shell::render_(compose|confirm)|SystemModule::render|\bsystem_page)\(",
                2,
            ),
            (r"M5Display::(text|walk)\(", 2),
        ],
    ),
    "setup: boot scan, one pending item": (
        LOOP_BUDGET,
        [
            (r"^void setup\(\)", 1),
            (r"Runtime::begin\(", 1),
            *gate.CHAINS["boot scan: check one pending item"][1],
            (r"?LfsStore::read\(", 1),
        ],
    ),
    # v0.2: the SD card as primary store (09 §5, R20)
    "loop: outbox.ack rejected, moved to bad/ on SD": (
        LOOP_BUDGET,
        [
            *_ACK,
            (r"?Runtime::apply_outbox\(", 1),
            (r"\boutbox_apply\(", 1),
            (r"?\{anonymous\}::run_action\(", 1),
            (r"\bmove_file\(", 1),
            (r"?\{anonymous\}::fat_commit\(", 1),
            *_SD,
        ],
    ),
    "loop: key saves a note on SD": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_keys\(", 1),
            (r"?Runtime::key_event\(", 1),
            (r"Shell::on_key\(", 1),
            (r"?Shell::compose_key_event\(", 1),
            (r"Shell::submit_compose\(", 1),
            (r"?Runtime::save_note\(", 1),
            (r"Runtime::save_item\(", 1),
            (r"Runtime::encode_persist\(", 1),
            (r"\boutbox_persist\(", 1),
            *_SD_COMMIT,
        ],
    ),
    "loop: projects cache written on SD": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"?Runtime::link_effect\(", 1),
            (r"?Runtime::deliver\(", 1),
            (r"Runtime::deliver_snapshot\(", 1),
            (r"Runtime::save_projects_cache\(", 1),
            *_SD_COMMIT,
        ],
    ),
    "loop: pending item read from SD and sent": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_outbox\(", 1),
            (r"?Runtime::send_item\(", 1),
            (r"\boutbox_read\(", 1),
            *_SD,
        ],
    ),
    "setup: migration, commit_file on SD": (LOOP_BUDGET, [*_MIGRATE, *_SD_COMMIT]),
    "setup: migration progress drawn": (
        LOOP_BUDGET,
        [
            *_MIGRATE,
            (r"MigrateScreen::on_item\(", 1),
            (r"?MigrateScreen::draw\(", 1),
            (r"M5Display::(text|walk)\(", 2),
        ],
    ),
    "setup: boot scan of the SD card": (
        LOOP_BUDGET,
        [
            (r"^void setup\(\)", 1),
            (r"Runtime::begin\(", 1),
            *gate.CHAINS["boot scan: SdStore list of an epoch directory"][1][:3],
            (r"VfsFatPort::list\(", 1),
            *gate.CHAINS["boot scan: SdStore list of an epoch directory"][1][3:],
            FATFS_STEP,
        ],
    ),
    # v0.3 recording (docs/11 §4.7): file.* parsed and answered on the loop
    "loop: file.* request answered at once": (
        LOOP_BUDGET,
        [
            *_RX_LINE,
            (r"Runtime::rec_request\(", 1),
            (r"Runtime::file_request\(", 1),
            (r"?Runtime::file_(get|del|list)\(", 1),
            (r"?Runtime::(send_data0|send_error|send_ack|send_items|queue_file)\(", 1),
            (r"?Runtime::(begin_reply|rec_apply)\(", 1),
            (r"Runtime::transmit\(", 1),
            *_ENCODE,
        ],
    ),
    "loop: file.data encoded and sent": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_rec\(", 1),
            (r"?Runtime::rec_event\(", 1),
            (r"?Runtime::file_reply\(", 1),
            (r"Runtime::send_data\(", 1),
            (r"Runtime::transmit\(", 1),
            (r"proto::Codec::encode\(", 1),
            (r"?\{anonymous\}::encode_data_line\(", 1),
            (r"proto::file_data_line\(", 1),
        ],
    ),
    "loop: file.items page fitted": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_rec\(", 1),
            (r"?Runtime::rec_event\(", 1),
            (r"?Runtime::file_reply\(", 1),
            (r"Runtime::send_items\(", 1),
            (r"?\bfile_fit_page\(", 1),
            (r"Runtime::items_fit\(", 1),
            *_ENCODE,
        ],
    ),
    "loop: file.items sent": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_rec\(", 1),
            (r"?Runtime::rec_event\(", 1),
            (r"?Runtime::file_reply\(", 1),
            (r"Runtime::send_items\(", 1),
            (r"Runtime::transmit\(", 1),
            *_ENCODE,
        ],
    ),
    "loop: recording key to the audio task": (
        LOOP_BUDGET,
        [
            *_LOOP,
            (r"Runtime::pump_keys\(", 1),
            (r"?Runtime::key_event\(", 1),
            (r"Shell::on_key\(", 1),
            (r"?Shell::rec_key_event\(", 1),
            (r"Runtime::rec_input\(", 1),
            (r"\brec::rec_input\(", 1),
            (r"?Runtime::rec_apply\(", 1),
            (r"?Runtime::rec_effect\(", 1),
            (r"?Runtime::post\(", 1),
        ],
    ),
    # v0.4.0 (docs/13 §5.4-§7): the Tab menu, the home launcher and the lock
    "loop: key in the Tab menu": (
        LOOP_BUDGET,
        [
            *_KEY,
            (r"Shell::on_key\(", 1),
            (r"?Shell::dispatch\(", 1),
            (r"?Shell::menu_key_event\(", 1),
            (r"\bmenu_step\(", 1),
            *_MENU_ROWS,
        ],
    ),
    "loop: render (Tab menu rows)": (
        LOOP_BUDGET,
        [*_RENDER, (r"?Shell::render_menu\(", 1), (r"\bmenu_draw\(", 1), *_MENU_ROWS],
    ),
    "loop: render (Tab menu drawn)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"?Shell::render_menu\(", 1),
            (r"\bmenu_draw\(", 1),
            (r"\bmenu_render\(", 1),
            (r"?\{anonymous\}::draw_(title|rows|row)\(", 2),
            *_TEXT,
        ],
    ),
    "loop: render (home launcher)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"?Shell::render_content\(", 1),
            (r"HomeModule::render(_tiles|_urgent|_greeting)?\(", 2),
            (r"?\{anonymous\}::tile\(", 1),
            *_TEXT,
        ],
    ),
    # v0.5 (docs/19 §6.3, §7): Mando and his bubble on home
    "loop: render (home: Mando)": (
        LOOP_BUDGET,
        [*_RENDER, (r"?Shell::render_content\(", 1), (r"HomeModule::render\(", 1), *_MANDO],
    ),
    "loop: render (locked: card, ● line)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"\brender_lock_screen\(", 1),
            (r"?\{anonymous\}::(draw_card|draw_rec_mark)\(", 1),
            (r"?\{anonymous\}::draw_(dozing|clock|chips|hints)\(", 1),
            (r"M5Display::(text|textWidth|walk)\(", 2),
        ],
    ),
    # v0.5 (docs/19 §2.3, §6.8): the lock card's glow and its Nunito clock
    "loop: render (locked: glow, clock)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"\brender_lock_screen\(", 1),
            (r"?\{anonymous\}::draw_card\(", 1),
            (r"?\{anonymous\}::draw_clock\(", 1),
            (r"RasterCanvas::(glow|digits)\(", 1),
            (r"theme::(glow|blit4)\(", 1),
        ],
    ),
    # v0.5 (docs/19 §6.6-§6.7): the memo and meeting pages, Mando, the big ● and timer
    "loop: render (recording page)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"?Shell::render_content\(", 1),
            (r"?Shell::render_rec\(", 1),
            (r"\brender_(memo|meeting)_page\(", 1),
            (r"?\{anonymous\}::draw_(mando|clock|stars|segment|cue|end_confirm)\(", 1),
            *_MANDO,
        ],
    ),
    # gh #53: the pages' legend of 1-9 and 0: the list measured, the plan cut, the room a
    # tick asks for (lib/shell/rec_legend)
    "loop: render (memo page legend)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"?Shell::render_content\(", 1),
            (r"?Shell::render_rec\(", 1),
            (r"\brender_memo_page\(", 1),
            (r"\bmemo_legend\(", 1),
            (r"\blegend_plan\(", 1),
            (r"?\{anonymous\}::(spot_of|cut_label)\(", 2),
            (r"\butf8_fit\(", 1),
            (r"M5Display::(textWidth|walk)\(", 2),
        ],
    ),
    "loop: render (meeting page legend)": (
        LOOP_BUDGET,
        [
            *_RENDER,
            (r"?Shell::render_content\(", 1),
            (r"?Shell::render_rec\(", 1),
            (r"\brender_meeting_page\(", 1),
            (r"\bmeeting_legend\(", 1),
            (r"?\{anonymous\}::meeting_list\(", 1),
            (r"\blegend_list\(", 1),
            (r"?\{anonymous\}::item\(", 1),
            (r"M5Display::(textWidth|walk)\(", 2),
        ],
    ),
    "loop: animate (the room the legend leaves)": (
        LOOP_BUDGET,
        [
            *_ANIMATE,
            (r"?Shell::rec_tick\(", 1),
            (r"\brec_room\(", 1),
            (r"\bmeeting_legend_rows\(", 1),
            (r"?\{anonymous\}::meeting_list\(", 1),
            (r"\blegend_list\(", 1),
            (r"?\{anonymous\}::item\(", 1),
            (r"M5Display::(textWidth|walk)\(", 2),
        ],
    ),
    # v0.5 S3 (docs/19 §3.5, §7.5): what a tick redraws, and the Tab sheet's slide frames
    "loop: animate (recording page: Mando, waveform, timeline)": (
        LOOP_BUDGET,
        [
            *_ANIMATE,
            (r"?Shell::rec_tick\(", 1),
            (r"\brec_(mando|wave|line)_part\(", 1),
            (r"?\{anonymous\}::draw_(timeline|wave|wave_bar|line_stars)\(", 2),
            *_MANDO,
        ],
    ),
    "loop: animate (home Mando, the lock card's z's)": (
        LOOP_BUDGET,
        [
            *_ANIMATE,
            (r"?Shell::(module_tick|doze_tick)\(", 1),
            (r"(HomeModule::animate|\block_z_part)\(", 1),
            (r"?\{anonymous\}::draw_(sleeper|zs)\(", 1),
            (r"RasterCanvas::glow\(", 1),
            *_MANDO,
        ],
    ),
    "loop: animate (Tab sheet slide)": (
        LOOP_BUDGET,
        [
            *_ANIMATE,
            (r"?Shell::slide_frame\(", 1),
            (r"Shell::render_menu_slide\(", 1),
            (r"\bmenu_draw_slide\(", 1),
            (r"\bmenu_render_slide\(", 1),
            (r"?\{anonymous\}::draw_(sheet|tabs|rows|row)\(", 2),
            *_TEXT,
        ],
    ),
    "loop: key while locked (the lock gate)": (
        LOOP_BUDGET,
        [*_KEY, (r"Shell::lock_gate\(", 1), (r"\block_key\(", 1), (r"?\{anonymous\}::g0_key\(", 1)],
    ),
    "setup: boot scan, LfsStore::list": (
        LOOP_BUDGET,
        [
            (r"^void setup\(\)", 1),
            (r"Runtime::begin\(", 1),
            (r"\boutbox_boot_scan\(", 1),
            (r"LfsStore::list\(", 1),
            (r"ScanVisitor::entry\(", 1),
            (r"\bscan_entry\(", 1),
            (r"\boutbox_insert_(bad|pending)\(", 1),
        ],
    ),
}
CHAINS = {title: (budget, chain(*steps)) for title, (budget, steps) in CHAINS.items()}


def _ours(f: gate.Frame) -> bool:
    return "cpa::" in f.name or bool(re.match(r"^void (setup|loop)\(\)", f.name))


def load_app_frames(build_dir: Path) -> list[gate.Frame]:
    files = [
        su for su in sorted(build_dir.glob("lib*/*/*.su")) if su.parent.name in APP_DIRS
    ]
    files += sorted(build_dir.glob("src/*.su"))
    return _frames_of(files)


def _frames_of(files: list[Path]) -> list[gate.Frame]:
    frames = []
    for su in files:
        for line in su.read_text().splitlines():
            m = gate._SU_LINE.match(line)
            if m:
                frames.append(gate.Frame(m["name"], int(m["size"]), m["qual"]))
    return frames


def main(argv: list[str], chains: dict | None = None) -> int:
    build_dir = Path(argv[0]) if argv else Path(".pio/build/cardputer-adv")
    app = load_app_frames(build_dir)
    if not app:
        print(
            f"stack budget: no application .su files under {build_dir} (build with -fstack-usage)"
        )
        return 1
    chains = CHAINS if chains is None else chains
    frames = (
        app
        + gate.load_frames(build_dir)
        + [
            gate.Frame("__framework_allowance__", FRAMEWORK, "static"),
            gate.Frame("__fatfs_allowance__", FATFS, "static"),
        ]
    )
    for title, (budget, steps) in chains.items():
        print(
            f"stack budget: {gate.chain_cost(frames, steps)[0]:5d} / {budget} B  {title}"
        )
    # Frame limits apply to our own functions; library templates instantiated in our
    # files (M5GFX effects, std::) are only counted where a chain goes through them.
    ours = [f for f in app if _ours(f)]
    bad = gate.check(ours, frame_max=APP_FRAME_MAX, caps={}, chains={})
    sized = [f if _ours(f) else f._replace(qual="static") for f in frames]
    bad += gate.check(sized, frame_max=1 << 30, caps={}, chains=chains)
    for line in bad:
        print(f"stack budget: FAIL {line}")
    biggest = max(app, key=lambda f: f.size)
    print(
        f"stack budget: {len(app)} app frames, largest {biggest.size} B ({biggest.name}), {len(bad)} violation(s)"
    )
    return 1 if bad else 0


def check_test_frames(build_dir: Path, suite: str) -> int:
    """The frames of one device suite's own sources (headers they include too)."""
    frames = _frames_of(sorted((build_dir / "test" / suite).glob("**/*.su")))
    if not frames:
        print(f"stack budget: no .su files for test/{suite} under {build_dir}")
        return 1
    bad = [f for f in frames if f.size > APP_FRAME_MAX]
    for f in bad:
        print(f"stack budget: FAIL {suite}: {f.name} uses {f.size} B > {APP_FRAME_MAX} B")
    return 1 if bad else 0


try:  # as a PlatformIO extra script: check after every link of the firmware
    Import("env")

    # Unit-test firmware has its own setup()/loop() and only some of the layers: the
    # on-device suite test/device/test_runtime_budget measures the real stack instead,
    # and each suite's own frames are capped.
    if "PIOTEST_RUNNING_NAME" not in env:
        env.AddPostAction(
            "$BUILD_DIR/${PROGNAME}.elf",
            lambda target, source, env: main([env.subst("$BUILD_DIR")]),
        )
    else:
        env.AddPostAction(
            "$BUILD_DIR/${PROGNAME}.elf",
            lambda target, source, env: check_test_frames(
                Path(env.subst("$BUILD_DIR")), env["PIOTEST_RUNNING_NAME"]
            ),
        )
except NameError:  # imported by pytest or run stand-alone
    pass

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
