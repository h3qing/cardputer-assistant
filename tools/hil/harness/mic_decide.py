"""Pure decision of the mic gain calibration over the sweep (mic gain brief §7); one capture is harness.mic_cal.

Each setting's gain change dG against the mean of the two baselines, the validity rules, the user's
predicted peak / RMS / SNR, the split among the +30 dB settings, the REG17 trim, the lead
recommendation, the WhisperWoof threshold, the B1 probe of the reference level, the verify pass at the
committed kMicGain (§6.7), the refusal to run while the real cpad may still have recordings on the card,
and the report table (numbers only).
"""

from __future__ import annotations

import math
import statistics
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from harness.mic_cal import (
    BASELINES,
    BY_LABEL,
    NEAR_CLIP,
    PREFERRED,
    SPLITS,
    SWEEP,
    UNITY,
    UNITY_DB,
    VOL_0DB,
    Capture,
    Setting,
    recommend_lead,
)

VOL_MAX = 0xD7  # REG17 trim range: 0xBF .. 0xD7 = 0 .. +12 dB
UNITY_MAG = 2
USER_PEAK_DBFS = -23.7  # the user's handheld memos at B1: max sample peak after the lead (brief §0)
USER_RMS_DBFS = -47.0
TARGET_DBFS, WINDOW_DBFS = -12.0, (-14.0, -10.0)
DRIFT_DB, UNITY_TOL_DB, APPLIED_TOL_DB, LINEAR_TOL_DB = 1.0, 0.5, 1.5, 1.0
CLIP_UP_TO_DB = 33.0
SPLIT_WITHIN_DB = 1.0
FAR_DB = 14.0  # speech at 1 m against handheld (brief §7.6)
PROBE_WINDOW, PROBE_AIM_DBFS = (-48.0, -27.0), -36.0
LEAD_KEEP_MS = 150


@dataclass(frozen=True, slots=True)
class Gain:
    tone: float  # dG from tone A, or from tone B when tone A is near clipping (dB)
    speech: float | None
    via: str  # "A" | "B"


def _delta(caps: Mapping[str, Capture], label: str, attr: str) -> float | None:
    vals = [getattr(caps[n], attr) for n in (label, *BASELINES)]
    return None if None in vals else vals[0] - statistics.fmean(vals[1:])


def gain(caps: Mapping[str, Capture], label: str) -> Gain:
    via_b = caps[label].tone_a_peak >= NEAR_CLIP
    tone = _delta(caps, label, "tone_b_db" if via_b else "tone_a_db")
    return Gain(tone, _delta(caps, label, "speech_db"), "B" if via_b else "A")


def _missing(caps: Mapping[str, Capture]) -> tuple[str, ...]:
    def why(c: Capture | None) -> str | None:
        if c is None:
            return "missing"
        if c.onset_ms is None or c.tone_a_db is None or c.tone_b_db is None:
            return "no tone"
        return "no speech level" if c.speech_db is None else None

    return tuple(f"{s.label}: {why(caps.get(s.label))}" for s in SWEEP if why(caps.get(s.label)))


def _setting_problems(s: Setting, c: Capture, g: Gain) -> tuple[str, ...]:
    nominal = s.nominal_db - BY_LABEL["B1"].nominal_db
    rules = (
        (abs(g.tone - nominal) > APPLIED_TOL_DB,
         f"applied: {s.label} dG {g.tone:+.2f} dB, nominal {nominal:+.2f} +- {APPLIED_TOL_DB}"),
        (abs(g.tone - g.speech) > LINEAR_TOL_DB,
         f"linear: {s.label} tone dG {g.tone:+.2f} vs speech dG {g.speech:+.2f} dB"),
        (s.nominal_db <= CLIP_UP_TO_DB and c.clipped > 0,
         f"clipped: {s.label} {c.clipped} samples at {s.nominal_db:+.1f} dB"),
    )  # fmt: skip
    return tuple(msg for bad, msg in rules if bad)


def validity(caps: Mapping[str, Capture]) -> tuple[str, ...]:
    """Brief §7.1; () when the sweep may decide."""
    missing = _missing(caps)
    if missing:
        return missing
    drift = abs(caps["B1"].tone_a_db - caps["B2"].tone_a_db)
    unity = gain(caps, UNITY).tone
    sweep = (
        (drift > DRIFT_DB, f"drift: |B1 - B2| {drift:.2f} dB > {DRIFT_DB}"),
        (abs(unity - UNITY_DB) > UNITY_TOL_DB, f"unity: U0 dG {unity:+.2f} dB, want {UNITY_DB:+.2f} +- {UNITY_TOL_DB}"),
    )
    own = tuple(p for s in SWEEP for p in _setting_problems(s, caps[s.label], gain(caps, s.label)))
    return tuple(msg for bad, msg in sweep if bad) + own


@dataclass(frozen=True, slots=True)
class Prediction:
    peak: float  # the user's handheld session peak at this setting, dBFS
    rms: float
    noise: float  # 100 ms p50 after the high-pass, dBFS
    snr: float


def predict(gain_db: float, noise_db: float, user_peak: float = USER_PEAK_DBFS,
            user_rms: float = USER_RMS_DBFS) -> Prediction:  # fmt: skip
    return Prediction(user_peak + gain_db, user_rms + gain_db, noise_db, user_rms + gain_db - noise_db)


def choose_split(snr: Mapping[str, float]) -> str:
    """PGA 10 / REG17 0xBF when its SNR is within 1 dB of the best +30 dB split, else the best."""
    best = max(snr, key=snr.__getitem__)
    return PREFERRED if PREFERRED in snr and snr[PREFERRED] >= snr[best] - SPLIT_WITHIN_DB else best


def trim(pga: int, vol: int, peak: float, target: float = TARGET_DBFS) -> tuple[int, int, float]:
    """REG17 = vol + round(2 (target - peak)), half a step up, within 0xBF..0xD7; one PGA step down while
    that would be under 0xBF. Returns (pga, REG17, the predicted peak there)."""
    want = vol + math.floor(2 * (target - peak) + 0.5)
    if want < VOL_0DB and pga > 0:
        return trim(pga - 1, vol, peak - 3.0, target)
    final = min(max(want, VOL_0DB), VOL_MAX)
    return pga, final, peak + (final - vol) / 2


def silence_rms(noise_db: float, rms_db: float) -> float:
    """CARDPUTER_SILENCE_RMS: the dB midpoint of the noise floor and the far speech level, float RMS."""
    return 10 ** ((noise_db + rms_db) / 40)


def headroom(caps: Mapping[str, Capture]) -> dict[str, Any]:
    clips = {n: caps[n].clipped if n in caps else None for n in ("N33", "N36")}
    return {**clips, "ok": all(v == 0 for v in clips.values())}


def _no_decision(problems: Sequence[str], head: Mapping[str, Any], split: str | None = None) -> dict[str, Any]:
    keys = ("snr_db", "pga", "vol", "vol_hex", "mag", "peak_dbfs", "rms_dbfs", "noise_dbfs", "silence_rms")
    return {"ok": False, "problems": list(problems), "split": split, **dict.fromkeys(keys), "headroom": dict(head)}


def decide(caps: Mapping[str, Capture], user_peak: float = USER_PEAK_DBFS,
           user_rms: float = USER_RMS_DBFS) -> dict[str, Any]:  # fmt: skip
    """Brief §7.1-7.4 and 7.6: the gain to ship, or ok false with the reasons (keep the constants)."""
    head = headroom(caps)
    problems = validity(caps)
    if problems:
        return _no_decision(problems, head)
    preds = {n: predict(gain(caps, n).tone, caps[n].noise.hp_db, user_peak, user_rms) for n in SPLITS}
    split = choose_split({n: p.snr for n, p in preds.items()})
    p = preds[split]
    pga, vol, peak = trim(BY_LABEL[split].pga, BY_LABEL[split].vol, p.peak)
    if not WINDOW_DBFS[0] <= peak <= WINDOW_DBFS[1]:
        why = f"final peak {peak:.2f} dBFS outside the window {list(WINDOW_DBFS)} at PGA {pga}, REG17 0x{vol:02X}"
        return _no_decision((why,), head, split)
    noise, speech = p.noise + peak - p.peak, p.rms + peak - p.peak
    return {"ok": True, "problems": [], "split": split, "snr_db": {n: round(q.snr, 2) for n, q in preds.items()},
            "pga": pga, "vol": vol, "vol_hex": f"0x{vol:02X}", "mag": UNITY_MAG, "peak_dbfs": round(peak, 2),
            "rms_dbfs": round(speech, 2), "noise_dbfs": round(noise, 2),
            "silence_rms": round(silence_rms(noise, speech - FAR_DB), 6), "headroom": head}  # fmt: skip


def lead_verdict(leads: Sequence[int | None]) -> dict[str, Any]:
    """Brief §7.5: keep kLeadMs = 150 only when the worst run, rounded up to 50 ms, is 150 or less."""
    rec = recommend_lead(leads)
    return {"leads": list(leads), "recommended": rec, "keep_150": rec is not None and rec <= LEAD_KEEP_MS}


def verify(b1: Capture, k: Capture, want: Setting, user_peak: float = USER_PEAK_DBFS) -> dict[str, Any]:
    """Brief §6.7: a capture at the firmware's own kMicGain (no debug.mic) against B1 of the same sitting: its
    gain within 1.5 dB of nominal, tone and speech agreeing, no clipping, every LSB in use at unity
    magnification, and the user's predicted peak in [-14, -10] dBFS."""
    expected = {"pga": want.pga, "vol": want.vol, "mag": want.mag}
    if None in (b1.tone_a_db, b1.tone_b_db, b1.speech_db, k.tone_a_db, k.tone_b_db, k.speech_db):
        no_level = "no tone or speech level in B1 or the kMicGain capture"
        return {"ok": False, "problems": [no_level], "expected": expected}
    g = gain({"B1": b1, "B2": b1, want.label: k}, want.label)  # one sitting: B1 is both baselines
    nominal, peak = want.nominal_db - BY_LABEL["B1"].nominal_db, user_peak + g.tone
    rules = (
        (abs(g.tone - nominal) > APPLIED_TOL_DB,
         f"applied: dG {g.tone:+.2f} dB, nominal {nominal:+.2f} +- {APPLIED_TOL_DB}"),
        (abs(g.tone - g.speech) > LINEAR_TOL_DB, f"linear: tone dG {g.tone:+.2f} vs speech dG {g.speech:+.2f} dB"),
        (k.clipped > 0, f"clipped: {k.clipped} samples at the M5 clamp"),
        (want.mag == UNITY_MAG and k.step != 1, f"resolution: sample step {k.step}, want 1 at unity magnification"),
        (not WINDOW_DBFS[0] <= peak <= WINDOW_DBFS[1], f"peak: predicted {peak:.2f} dBFS outside {list(WINDOW_DBFS)}"),
    )  # fmt: skip
    problems = [msg for bad, msg in rules if bad]
    return {"ok": not problems, "problems": problems, "expected": expected, "nominal_db": round(nominal, 2),
            "gain_db": round(g.tone, 2), "speech_gain_db": round(g.speech, 2), "via": g.via,
            "peak_dbfs": round(peak, 2), "rms_dbfs": round(USER_RMS_DBFS + g.tone, 2),
            "noise_hp_dbfs": round(k.noise.hp_db, 2), "clipped": k.clipped, "step": k.step,
            "lead_ms": k.lead_ms}  # fmt: skip


def probe_step(tone_a_db: float, gain_db: float, max_gain_db: float) -> tuple[str, float | None]:
    """Brief §6.3 at B1: ok in [-48, -27] dBFS; else the reference gain that puts tone A at -36, within the
    reference's headroom; too_quiet when even that stays under -48 (move the device closer)."""
    if PROBE_WINDOW[0] <= tone_a_db <= PROBE_WINDOW[1]:
        return "ok", gain_db
    new = min(gain_db + PROBE_AIM_DBFS - tone_a_db, max_gain_db)
    return ("too_quiet", None) if tone_a_db + new - gain_db < PROBE_WINDOW[0] else ("rescale", new)


def status_problem(doc: Mapping[str, Any]) -> str | None:
    """`cpa status --json` of the real cpad: why mic_cal must not start, or None (card known empty)."""
    recs = doc.get("recordings") or {}
    on, pulling = recs.get("on_device"), recs.get("pulling")
    if not doc.get("running"):
        return "cpad is not running, so nothing confirms the card is empty: start it and let it pull first"
    if on is None:
        return f"cpad cannot see the card (link {doc.get('link')}): resume it (cpa pause 1) and let it pull first"
    if on:
        return f"{on} recording(s) still on the device: the temporary HIL daemon would pull and delete them"
    return "cpad is still pulling a recording: wait until it is done" if pulling else None


# --- report (numbers only) ---------------------------------------------------------------------

COLUMNS = (
    ("#", "label"), ("PGA", "pga"), ("REG17", "vol"), ("mag", "mag"), ("nominal dG", "nominal_db"),
    ("tone A dBFS", "tone_a_dbfs"), ("dG", "gain_db"), ("via", "via"), ("speech dG", "speech_gain_db"),
    ("noise HP p50 dBFS", "noise_hp_dbfs"), ("clipped", "clipped"), ("step", "step"), ("lead ms", "lead_ms"),
    ("P dBFS", "peak_pred_dbfs"), ("R dBFS", "rms_pred_dbfs"), ("SNR dB", "snr_db"),
)  # fmt: skip


def _r(v: float | None, digits: int = 2) -> float | None:
    return None if v is None else round(v, digits)


def _row(s: Setting, c: Capture, caps: Mapping[str, Capture] | None) -> dict[str, Any]:
    g = gain(caps, s.label) if caps is not None and c.tone_a_db is not None else None
    p = predict(g.tone, c.noise.hp_db) if g is not None and g.tone is not None else None
    return {
        "label": s.label, "pga": s.pga, "vol": f"0x{s.vol:02X}", "mag": s.mag,
        "nominal_db": round(s.nominal_db - BY_LABEL["B1"].nominal_db, 2), "onset_ms": c.onset_ms,
        "tone_a_dbfs": _r(c.tone_a_db), "tone_b_dbfs": _r(c.tone_b_db), "speech_dbfs": _r(c.speech_db),
        "gain_db": _r(g.tone) if g else None, "via": g.via if g else None,
        "speech_gain_db": _r(g.speech) if g else None,
        "noise_bb": [round(v, 1) for v in c.noise.bb], "noise_hp": [round(v, 1) for v in c.noise.hp],
        "noise_hp_dbfs": round(c.noise.hp_db, 1), "clipped": c.clipped, "peak": c.peak,
        "transient_peak": c.transient_peak, "step": c.step, "lead_ms": c.lead_ms,
        "peak_pred_dbfs": _r(p.peak) if p else None, "rms_pred_dbfs": _r(p.rms) if p else None,
        "snr_db": _r(p.snr) if p else None,
    }  # fmt: skip


def rows(caps: Mapping[str, Capture]) -> list[dict[str, Any]]:
    """One row per captured setting in sweep order; gains only once both baselines have a tone."""
    based = all(n in caps and caps[n].tone_a_db is not None for n in BASELINES)
    return [_row(s, caps[s.label], caps if based else None) for s in SWEEP if s.label in caps]


def _cell(v: Any) -> str:
    return "-" if v is None else str(v)


def _decision_line(d: Mapping[str, Any]) -> str:
    if not d["ok"]:
        return "No decision, keep the current constants: " + "; ".join(d["problems"])
    h = d["headroom"]
    return (f"Decision: PGA {d['pga']}, REG17 {d['vol_hex']}, mag {d['mag']} (split {d['split']}): peak "
            f"{d['peak_dbfs']} dBFS, speech RMS {d['rms_dbfs']} dBFS, noise {d['noise_dbfs']} dBFS; "
            f"CARDPUTER_SILENCE_RMS {d['silence_rms']}; N33 / N36 clipped {h['N33']} / {h['N36']}.")  # fmt: skip


def _lead_line(lead: Mapping[str, Any]) -> str:
    if lead["recommended"] is None:
        return f"Lead: not measured (runs {lead['leads']})."
    keep = "keep kLeadMs = 150" if lead["keep_150"] else f"raise kLeadMs to {lead['recommended']}"
    return f"Lead: runs {lead['leads']} ms -> {lead['recommended']} ms ({keep})."


def markdown(table: Sequence[Mapping[str, Any]], decision: Mapping[str, Any], lead: Mapping[str, Any]) -> str:
    head = "| " + " | ".join(h for h, _ in COLUMNS) + " |"
    rule = "|" + "---|" * len(COLUMNS)
    body = ["| " + " | ".join(_cell(r[k]) for _, k in COLUMNS) + " |" for r in table]
    return "\n".join(["# mic_cal", "", head, rule, *body, "", _decision_line(decision), "", _lead_line(lead)]) + "\n"
