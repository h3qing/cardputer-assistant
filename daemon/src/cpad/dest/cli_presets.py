"""cpa dest presets | add --preset/--app/--preset-file | test --send | verify (docs/25 §3.1, §5.5).

Adding from a preset copies its template, limits and map into destinations.json and pins the app that
handles it: the bundle id found by LaunchServices and its signing Team ID (§5.3). A url's scheme must be one
the app registers. `cpa dest test ID --send` hands one marked test text to the app, through the same adapter
as a capture; `cpa dest verify ID --draft` (the text appeared and was not sent) then makes a destination of a
shipped preset a draft, and only after such a test; `--act` keeps it confirmed on the device. Nothing here
names an app: everything app-specific is a preset file.
"""

from __future__ import annotations

import argparse
import json
import re
import secrets
import time
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path
from typing import Any

from cpad.config.settings import Paths
from cpad.dest import clip_clear
from cpad.dest.catalog import Catalog, DestsError, bind, by_key, find, free_slot, put, state_of
from cpad.dest.cli_common import STATE_TEXT, opt_in, record, root
from cpad.dest.config import dests_path, load_catalog, load_dests
from cpad.dest.handlers import AppAdapter, UrlAdapter
from cpad.dest.handoff import draft_target, handed_text
from cpad.dest.macos import APP_ROOTS, AppInfo, LaunchServices, Mac, Signature, installed_apps
from cpad.dest.model import AppOpts, Capture, Delivery, DeliveryResult, Dest, FileOpts, UrlOpts
from cpad.dest.pasteboard import ClipWriter, NativePasteboard
from cpad.dest.paths import check_place
from cpad.dest.presets import Preset, PresetError, denied, load_deny, load_presets, map_entry, parse_preset
from cpad.dest.records import dest_handed
from cpad.dest.template import scheme_of
from cpad.dest.waiting import TEST_PREFIX, recent_drafts
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.i18n import T

MAC: Callable[[], Mac] = LaunchServices  # tests swap in fakes: nothing real is opened or written
WRITER: Callable[[], ClipWriter] = lambda: ClipWriter(NativePasteboard(), clear_later=clip_clear.spawn)  # noqa: E731
APPS_ROOTS: tuple[Path, ...] = APP_ROOTS


def _installed(mac: Mac, bundle: str) -> AppInfo | None:
    paths = mac.app_paths(bundle)
    return mac.info(paths[0]) if paths else None


def _preset_row(p: Preset, mac: Mac) -> dict[str, Any]:
    info = _installed(mac, p.bundle)
    return {"preset": p.preset, "label": p.label, "kind": p.kind, "id": f"{p.kind}.{p.name}", "key_hint": p.key_hint,
            "leaves_mac": p.leaves_mac, "effect": p.effect or "unknown", "may_be_draft": p.may_be_draft,
            "shipped": p.shipped, "installed": info is not None, "version": info.version if info else None,
            "note": p.note, "takes_map": p.map_pattern is not None}  # fmt: skip


def dest_presets(paths: Paths, args: argparse.Namespace) -> int:
    if args.apps:
        apps = [{"name": a.name, "bundle": a.bundle, "version": a.version} for a in installed_apps(APPS_ROOTS)]
        print(json.dumps(apps, ensure_ascii=False) if args.json else "\n".join(
            f"{a['name']:<28} {a['bundle']:<40} {a['version']}" for a in apps))  # fmt: skip
        return 0
    found, mac = load_presets(paths.home), MAC()
    rows = [r for r in (_preset_row(p, mac) for p in found.presets) if r["installed"] or not args.installed]
    if args.json:
        print(json.dumps({"presets": rows, "problems": list(found.problems)}, ensure_ascii=False))
        return 0
    for problem in found.problems:
        print(f"presets: {problem}")
    for r in rows:
        where = f"installed {r['version']}" if r["installed"] else "not installed"
        print(f"{r['preset']:<12} {r['id']:<14} {(r['key_hint'] or '-').upper()}  {r['effect']:<8} {where}")
    return 0


# --- add ----------------------------------------------------------------------------------------


def _usable(mac: Mac, app: Path, scheme: str | None) -> tuple[AppInfo, Signature] | None:
    info, signature = mac.info(app), mac.signature(app)
    if info is None or not signature.valid or (signature.team is None and not signature.apple):
        return None
    return (info, signature) if scheme is None or scheme in info.schemes else None


def _pinned(mac: Mac, bundle: str, scheme: str | None) -> tuple[AppInfo, str | None]:
    """The first copy of bundle that is signed by a certificate Apple issued, with a Team ID (or Apple's own), and
    registers scheme: (its info, its Team ID). An app deny.json names is refused for every kind."""
    if denied(bundle, load_deny()):
        raise DestsError("this app may not handle destinations")
    paths = mac.app_paths(bundle)
    if not paths:
        raise DestsError(f"no app {bundle} on this Mac")
    found = next((u for u in (_usable(mac, p, scheme) for p in paths) if u is not None), None)
    if found is not None:
        return found[0], found[1].team
    info, signature = mac.info(paths[0]), mac.signature(paths[0])
    if info is None or not signature.valid:
        raise DestsError(f"{paths[0]} is not signed by a certificate Apple issued")
    if signature.team is None and not signature.apple:
        raise DestsError(f"{info.name} has no Team ID: only Apple's own apps are pinned without one")
    raise DestsError(f"{info.name} does not handle {scheme}:// links")


def _maps(preset: Preset, raw: list[str] | None) -> tuple[tuple[str, str], ...]:
    deny, pairs = load_deny(), {}
    for item in raw or []:
        project, sep, value = item.partition("=")
        if not sep or not re.fullmatch(r"\*|[a-z0-9][a-z0-9-]{0,11}", project):
            raise DestsError("--map takes PROJECT=VALUE (or *=VALUE for every project)")
        pairs[project] = map_entry(preset, value, deny)
    return tuple(sorted(pairs.items()))


def _url_opts(preset: Preset, team: str | None, args: argparse.Namespace) -> UrlOpts:
    assert preset.template is not None
    return UrlOpts(preset.template, preset.bundle, team, effect="draft" if preset.may_be_draft else "act",
                   verified=None, focus=preset.focus, max_text_utf16=preset.max_text_utf16, max_url=preset.max_url,
                   overflow=preset.overflow, map=_maps(preset, args.map), preset=preset.preset)  # fmt: skip


def _dest(cat: Catalog, preset: Preset, args: argparse.Namespace, paths: Paths) -> Dest:
    if args.map and preset.kind != "url":
        raise DestsError("--map is only for url destinations")
    name = args.name or preset.name
    before = find(cat, f"{preset.kind}.{name}")
    base = Dest(f"{preset.kind}.{name}", preset.kind, preset.label, before.slot if before else free_slot(cat),
                leaves_mac=preset.leaves_mac and not args.local)  # fmt: skip
    if preset.kind == "url" and args.local:
        raise DestsError("a url destination always leaves the Mac (the app may sync its drafts)")
    info, team = _pinned(MAC(), preset.bundle, scheme_of(preset.template) if preset.template else None)
    if preset.kind == "url":
        return replace(base, url=_url_opts(preset, team, args))
    if preset.kind == "app":
        return replace(base, app=AppOpts(preset.bundle, team))
    if not args.root:
        raise DestsError(f"{preset.label} needs --root: the folder to write into")
    opts = FileOpts(root(args.root), args.path or preset.file_path or "")
    check_place(opts, Path.home(), denied=(paths.home,))
    return replace(base, file=opts)


def _app_preset(bundle: str, args: argparse.Namespace) -> Preset:
    info, _ = _pinned(MAC(), bundle, None)
    name = args.name or (re.sub(r"[^a-z0-9-]+", "-", info.name.lower()).strip("-")[:12].strip("-") or "app")
    label = (args.label or info.name).encode("utf-8")[:24].decode("utf-8", "ignore") or name
    note = "The text goes to the clipboard and the app comes forward; you press Cmd-V there, cpad never does."
    return Preset("app", name, label, "app", bundle, True, note, shipped=False)


def _chosen(paths: Paths, args: argparse.Namespace) -> Preset:
    if args.app:
        return _app_preset(args.app, args)
    if args.preset_file:
        try:
            doc = json.loads(Path(args.preset_file).read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise PresetError(f"{args.preset_file}: cannot be read as JSON") from exc
        return parse_preset(doc, shipped=False, deny=load_deny())
    found = load_presets(paths.home).find(args.preset)
    if found is None:
        raise DestsError(f"no preset {args.preset}: cpa dest presets lists them")
    return found


def _key(cat: Catalog, dest: Dest, preset: Preset, args: argparse.Namespace) -> str | None:
    """--key, else the preset's hint while no other destination holds it."""
    if args.key is not None:
        return args.key
    holder = by_key(cat, preset.key_hint) if preset.key_hint else None
    return preset.key_hint if preset.key_hint and (holder is None or holder.id == dest.id) else dest.key


def add_from_preset(paths: Paths, args: argparse.Namespace) -> int:
    cat = load_dests(dests_path(paths))
    preset = _chosen(paths, args)
    draft = _dest(cat, preset, args, paths)
    draft = replace(draft, label=args.label or draft.label, profiles=tuple(args.profile or ()))
    changed = put(cat, draft)
    key = _key(changed, draft, preset, args)
    changed = bind(changed, draft.id, key) if key is not None else changed
    dest = find(changed, draft.id)
    assert dest is not None
    allowed = opt_in(dest, args.yes, preset.note)
    if allowed is None:
        return 1
    record(paths, put(changed, allowed), "add", allowed)
    print(f"{allowed.id}: {STATE_TEXT[state_of(allowed)]}, slot {allowed.slot}, key {(allowed.key or '-').upper()}")
    if allowed.url is not None:
        print(f"next: cpa dest test {allowed.id} --send, then cpa dest verify {allowed.id} --draft or --act")
    return 0


# --- test --send, verify ------------------------------------------------------------------------


def _clip_on(paths: Paths) -> bool:
    clip = find(load_catalog(dests_path(paths)), "clip")
    return clip is not None and state_of(clip) == "on"


def _draft_held(paths: Paths, dest: Dest) -> bool:
    """A real capture filled this draft within DRAFT_HOLD_S: the app would keep it and drop the test silently.
    Earlier tests do not count (one that did not appear must not block the next)."""
    if dest.url is None:
        return False
    real = [e for e in EventLog(paths.events).load() if not str(e.get("item", "")).startswith(TEST_PREFIX)]
    return (dest.id, draft_target(dest.url, None)) in recent_drafts(real, time.time())


def marked_text(marker: str) -> str:
    """The text a test hands over, in the language of the moment: what cli_test then says to look for."""
    return T(f"测试 {marker}", f"test {marker}")


def hand_test(paths: Paths, dest: Dest) -> tuple[DeliveryResult, str]:
    """Hands one marked test text to a url / app destination, as a capture would go (by cli); (result, marker).
    A text that was handed is recorded as dest.handed, its item test-<marker>. Not handed (E_DRAFT_BUSY) while a
    capture's draft still waits in that app."""
    if dest.kind not in ("url", "app") or state_of(dest) != "on":
        raise DestsError("--send is for url and app destinations that are on")
    marker = f"{secrets.randbelow(9000) + 1000}"
    if _draft_held(paths, dest):
        return DeliveryResult(False, "E_DRAFT_BUSY"), marker
    cap = Capture(f"{TEST_PREFIX}{marker}", "typed", dest.id, "pick", None, marked_text(marker))
    mac, writer = MAC(), WRITER()
    adapter = UrlAdapter(mac, writer, lambda: _clip_on(paths)) if dest.kind == "url" else AppAdapter(mac, writer)
    d = Delivery(f"{cap.ref}:{dest.id}", cap, dest, 1, "cli")
    result = adapter.deliver(d)
    if result.ok and result.via is not None:
        handed = dest_handed(d, at=iso(time.time()), via=result.via, text=handed_text(cap) or "", ref=result.ref)
        EventLog(paths.events).append([handed])
    return result, marker


def _tested(paths: Paths, dest_id: str) -> bool:
    """A test went through dest_id's link since it was last added (not an overflow to the clipboard)."""
    tested = False
    for e in EventLog(paths.events).load():
        if e.get("ev") == "dest.changed" and e.get("id") == dest_id and e.get("op") == "add":
            tested = False
        elif e.get("ev") == "dest.handed" and e.get("dest") == dest_id and e.get("via") == "url":
            tested = tested or str(e.get("item", "")).startswith(TEST_PREFIX)
    return tested


def verify(paths: Paths, dest_id: str, *, draft: bool) -> int:
    """Records this Mac's answer: draft (it appeared and was not sent; only after a test) or act."""
    cat = load_dests(dests_path(paths))
    dest = find(cat, dest_id)
    if dest is None or dest.url is None:
        raise DestsError(f"{dest_id}: only url destinations are verified")
    if draft and dest.url.effect != "draft":
        raise DestsError(f"{dest_id} can never be a draft: its preset is yours or says it acts")
    if draft and not _tested(paths, dest.id):
        raise DestsError(f"first send a test: cpa dest test {dest.id} --send")
    verified = replace(dest, url=replace(dest.url, verified="draft" if draft else "act"))
    record(paths, put(cat, verified), "verify", verified)
    print(f"{dest.id}: {'fills a draft only' if draft else 'may act by itself: this Mac does not send to it'}")
    return 0


def dest_verify(paths: Paths, args: argparse.Namespace) -> int:
    return verify(paths, args.id, draft=args.draft)


def set_map(paths: Paths, dest_id: str, pairs: list[str]) -> Dest:
    """url.<name>'s {p} per project: PROJECT=VALUE or a link to it (*: every project), PROJECT=- forgets one."""
    cat = load_dests(dests_path(paths))
    dest = find(cat, dest_id)
    preset = load_presets(paths.home).find(dest.url.preset or "") if dest and dest.url else None
    if dest is None or dest.url is None or preset is None:
        raise DestsError(f"{dest_id}: only url destinations added from a preset take a map")
    kept = {k: v for k, v in dest.url.map if f"{k}=-" not in pairs}
    added = dict(_maps(preset, [p for p in pairs if not p.endswith("=-")]))
    changed = replace(dest, url=replace(dest.url, map=tuple(sorted({**kept, **added}.items()))))
    record(paths, put(cat, changed), "map", changed)
    return changed


def ls_fields(dest: Dest, presets: dict[str, Preset]) -> dict[str, Any]:
    """What cpa dest ls adds for url / app: handler; url: effect (what it may become), verified, preset, newer_preset
    and takes_map (its preset takes a Bot or other {p} value: cpa dest map)."""
    pinned = dest.url or dest.app
    if pinned is None:
        return {}
    out: dict[str, Any] = {"handler": {"bundle": pinned.bundle, "team": pinned.team}}
    if dest.url is None:
        return out
    shipped = presets.get(dest.url.preset or "")
    newer = shipped is not None and (shipped.template, shipped.focus, shipped.max_text_utf16, shipped.max_url) != (
        dest.url.template, dest.url.focus, dest.url.max_text_utf16, dest.url.max_url)  # fmt: skip
    return {**out, "effect": dest.url.effect, "verified": dest.url.verified, "preset": dest.url.preset,
            "newer_preset": newer, "takes_map": shipped is not None and shipped.map_pattern is not None}  # fmt: skip


def does_now(row: dict[str, Any]) -> str:
    """What a url row of cpa dest ls does on this Mac now: draft only after the test said so."""
    if row.get("verified") == "draft":
        return "draft"
    return "untested" if row.get("effect") == "draft" and row.get("verified") is None else "acts"


def add_flags(add: argparse.ArgumentParser) -> None:
    source = add.add_mutually_exclusive_group()
    source.add_argument("--preset", help="add from a preset (cpa dest presets)")
    source.add_argument("--app", metavar="BUNDLE", help="any app: clipboard + bring it forward (app.<name>)")
    source.add_argument("--preset-file", metavar="PATH", help="your own preset file (always confirmed on the device)")
    add.add_argument("--name", help="the <name> of <kind>.<name> (default: the preset's)")
    add.add_argument("--map", action="append", metavar="PROJECT=VALUE", help="url {p} per project, or *=VALUE")


def add_parsers(actions: Any, wrap: Callable[[Any], Any]) -> None:
    presets = actions.add_parser("presets", help="apps destinations can be added for (data, never code)")
    which = presets.add_mutually_exclusive_group()
    which.add_argument("--installed", action="store_true", help="only presets whose app is on this Mac")
    which.add_argument("--apps", action="store_true", help="every app on this Mac, for --app")
    presets.add_argument("--json", action="store_true")
    presets.set_defaults(fn=wrap(dest_presets))
    verify = actions.add_parser("verify", help="record what the test send did: --draft or --act")
    verify.add_argument("id")
    answer = verify.add_mutually_exclusive_group(required=True)
    answer.add_argument("--draft", action="store_true", help="it appeared and was not sent")
    answer.add_argument("--act", action="store_true", help="it was sent, or did something, by itself")
    verify.set_defaults(fn=wrap(dest_verify))
