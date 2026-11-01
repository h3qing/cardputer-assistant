"""M3 device gates on the hwtest firmware (docs/gates/m3-device-gates.md).

    python hwtest_host.py xfer-sweep [--quick] [--no-soak] [--out FILE]     # G-A1
    python hwtest_host.py xfer-sd [--file-kb 9600]                          # G-A1: SD read pattern
    python hwtest_host.py xfer --enc b64 --len 1380 --depth 4 --ring 4 --src sd [--secs 10]
    python hwtest_host.py cue-sweep [--out FILE]                            # G-A5 (agent part)
    python hwtest_host.py cue-listen [--vols 32,64,128,192,255]             # G-A5 (by ear)
    python hwtest_host.py cue-silent [--gap 0.5]                            # G-A5 lead_ms, v0.3.1 start
    python hwtest_host.py mic-capture --mag 16 --pga 0 --secs 60 --label 30cm [--discard]  # G-A10
    python hwtest_host.py gate-clean                                        # remove gate files on the card
    python hwtest_host.py mer --ref REF.txt --hyp HYP.txt                   # G-A10 MER, no device

Opens the port the safe way (hwtest_host.open_port), handshakes with !info, writes one
JSON report (default under ../captures/, which git ignores) and prints a short table.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime

import gate_cue
import gate_mer
import gate_mic
import gate_xfer
from gate_link import GateLink
from hwtest_host import find_port, open_port
from sd_run import handshake, pull

CAPTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "captures")
XFER_FILE_KB = 2048  # /sd/cpa_hwtest/xfer.bin: SD reads wrap around it


def _out(path, stem):
    return path or os.path.join(CAPTURES, f"{stem}-{datetime.now():%Y%m%d-%H%M%S}.json")


def _save(path, report):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(f"saved {path}", file=sys.stderr)


def _clean(ev):
    return {k: v for k, v in (ev or {}).items() if not k.startswith("_")}


def _xfer_row(link, cfg):
    run = gate_xfer.run_xfer(link, cfg)
    summary = gate_xfer.summarize_run(run)
    s = summary
    print(f"{cfg.label():24s} {str(s['kbs']):>7s} KB/s  wire {str(s['wire_kbs']):>7s}  "
          f"rtt p95 {s['rtt'].get('p95')} ms  cpu1 {s['cpu1']}  gap {s['gap_max_ms']} ms  ok={s['ok']} {s['err']}",
          flush=True)
    raw = {"start": _clean(run["start"]), "end": run["end"], "xs": run["xs"], "rtt_ms": run["rtt_ms"]}
    return {"config": cfg.as_dict(), "summary": summary, "raw": raw}


def cmd_xfer_sweep(link, args):
    link.send(f"!xfermk {XFER_FILE_KB}")
    made = _clean(link.wait_for("xfermk", 120.0))
    configs = list(gate_xfer.SWEEP)
    if args.quick:
        configs = [c for c in configs if c.depth in (0, 4)]
    if not args.no_soak:
        configs += list(gate_xfer.SOAK)
    rows = [_xfer_row(link, c) for c in configs]
    report = {"gate": "G-A1", "xfermk": made, "rows": rows,
              "encoding": gate_xfer.decide_encoding(rows), "ring": gate_xfer.choose_ring(rows)}
    print(json.dumps({"encoding": report["encoding"], "ring": report["ring"]}), flush=True)
    return report


def cmd_xfer_sd(link, args):
    link.send(f"!xfermk {args.file_kb}")
    made = _clean(link.wait_for("xfermk", 300.0))
    rows = [_xfer_row(link, c) for c in gate_xfer.SD_SWEEP]
    return {"gate": "G-A1 sd", "xfermk": made, "rows": rows}


def cmd_xfer(link, args):
    cfg = gate_xfer.XferConfig(args.enc, args.len, args.depth, args.ring, args.src, args.secs)
    if cfg.src == "sd":
        link.send(f"!xfermk {XFER_FILE_KB}")
        link.wait_for("xfermk", 120.0)
    return {"gate": "G-A1", "rows": [_xfer_row(link, cfg)]}


def _cue_runs(link, sweep, gap):
    runs = []
    for i, cue in enumerate(sweep):
        r = gate_cue.run_cue(link, cue)
        c = r["cue"]
        print(f"{i:3d} {cue.pattern}/{cue.hammer} vol {cue.vol}: start {c.get('start_us')} us  "
              f"loop_max {c.get('loop_max_us')} us  bad_keys {c.get('bad_keys')}  i2c_err {c.get('i2c_err')}  "
              f"rb {c.get('spk_rb')}/{c.get('mic_rb')}", flush=True)
        runs.append(r)
        time.sleep(gap)
    return runs


def cmd_cue_sweep(link, args):
    runs = _cue_runs(link, gate_cue.CUE_SWEEP, args.gap)
    summary = gate_cue.summarize_cue(runs)
    verdicts = {mode: gate_cue.gate_verdict(summary, mode) for mode in ("lock", "lockyield")}
    print(json.dumps(verdicts), flush=True)
    return {"gate": "G-A5", "runs": runs, "summary": summary, "verdicts": verdicts}


def cmd_cue_silent(link, args):
    runs = _cue_runs(link, gate_cue.SILENT_SWEEP, args.gap)
    summary = gate_cue.summarize_cue(runs)
    g = summary.get("silent/lockyield", {"lead_ms": {}, "leads": []})
    lead = {"lead_ms": g["lead_ms"], "worst_rounded_up": gate_cue.recommend_lead(g["leads"])}
    print(json.dumps(lead), flush=True)
    return {"gate": "G-A5 silent", "runs": runs, "summary": summary, "lead": lead}


def cmd_cue_listen(link, args):
    played = []
    for vol in (int(v) for v in args.vols.split(",")):
        for cue in (gate_cue.CueRun("memo", 80, vol, "off", 200), gate_cue.CueRun("meet", 100, vol, "off", 200)):
            print(f"now playing: {cue.pattern} start cue at volume {vol}", flush=True)
            played.append(gate_cue.run_cue(link, cue)["cue"])
            time.sleep(args.gap)
    return {"gate": "G-A5 listen", "played": played}


def cmd_mic(link, args):
    run = gate_mic.MicRun(args.mag, args.pga, args.secs)
    stem = f"g-a10-{args.label}-m{args.mag}-p{args.pga}-{datetime.now():%Y%m%d-%H%M%S}"
    wav = os.path.join(CAPTURES, stem + ".wav")
    os.makedirs(CAPTURES, exist_ok=True)
    print(f"recording {args.secs} s on the device (mag {args.mag}, PGA +{3 * args.pga} dB) ...", flush=True)
    report = gate_mic.run_mic(link, run, wav, pull)
    report["label"] = args.label
    if args.discard and os.path.exists(wav):
        os.remove(wav)
        report["wav"] = None
    else:
        report["wav"] = wav
    print(json.dumps(report.get("levels")), flush=True)
    return report


def cmd_gate_clean(link, args):
    link.send("!gateclean")
    return {"gateclean": _clean(link.wait_for("gateclean", 30.0))}


COMMANDS = {
    "xfer-sweep": (cmd_xfer_sweep, "g-a1"),
    "xfer-sd": (cmd_xfer_sd, "g-a1-sd"),
    "xfer": (cmd_xfer, "g-a1-one"),
    "cue-sweep": (cmd_cue_sweep, "g-a5"),
    "cue-listen": (cmd_cue_listen, "g-a5-listen"),
    "cue-silent": (cmd_cue_silent, "g-a5-silent"),
    "mic-capture": (cmd_mic, "g-a10"),
    "gate-clean": (cmd_gate_clean, "gate-clean"),
}


def parse(argv):
    ap = argparse.ArgumentParser(prog="hwtest_host.py")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in COMMANDS:
        p = sub.add_parser(name)
        p.add_argument("--out", default=None)
        p.add_argument("--gap", type=float, default=0.5, help="pause between runs (s)")
    sub.choices["xfer-sweep"].add_argument("--quick", action="store_true")
    sub.choices["xfer-sweep"].add_argument("--no-soak", action="store_true")
    sub.choices["xfer-sd"].add_argument("--file-kb", type=int, default=9375)  # 9,600,000 B: a 5-min segment
    x = sub.choices["xfer"]
    x.add_argument("--enc", default="b64")
    x.add_argument("--len", type=int, default=1380)
    x.add_argument("--depth", type=int, default=4)
    x.add_argument("--ring", type=int, default=4)
    x.add_argument("--src", default="ram")
    x.add_argument("--secs", type=int, default=10)
    sub.choices["cue-listen"].add_argument("--vols", default="32,64,128,192,255")
    sub.choices["cue-listen"].set_defaults(gap=2.0)
    m = sub.choices["mic-capture"]
    m.add_argument("--mag", type=int, default=16)
    m.add_argument("--pga", type=int, default=0)
    m.add_argument("--secs", type=int, default=60)
    m.add_argument("--label", default="take")
    m.add_argument("--discard", action="store_true", help="keep only the numbers, delete the WAV")
    return ap.parse_args(argv)


def mer_main(argv):
    ap = argparse.ArgumentParser(prog="hwtest_host.py mer")
    ap.add_argument("--ref", required=True)
    ap.add_argument("--hyp", required=True, nargs="+")
    args = ap.parse_args(argv)
    with open(args.ref, encoding="utf-8") as f:
        ref = f.read()
    for path in args.hyp:
        with open(path, encoding="utf-8") as f:
            print(f"{path}: MER {gate_mer.mer(ref, f.read()):.1f}%")
    return 0


def main(argv):
    if argv and argv[0] == "mer":
        return mer_main(argv[1:])
    args = parse(argv)
    fn, stem = COMMANDS[args.cmd]
    device = find_port()
    if not device:
        sys.exit("no 303A:1001 device found")
    ser = open_port(device)
    try:
        link = GateLink(ser)
        hs = handshake(link)
        if not hs["ok"]:
            sys.exit("no hello from hwtest (is the hwtest firmware flashed?)")
        report = {"when": datetime.now().isoformat(timespec="seconds"), "hello": _clean(hs["hello"])}
        report.update(fn(link, args))
    finally:
        ser.close()
    _save(_out(args.out, stem), report)
    return 0
