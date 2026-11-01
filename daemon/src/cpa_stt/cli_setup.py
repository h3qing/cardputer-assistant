"""`cpa-stt setup [MODEL...] [--yes]`: link existing models into $CPA_HOME/models (docs/22 §4.4, §4.13).

With paths: link those. Without: for each model the example needs, look in the known folders, then with
Spotlight, show the largest match and link it after a yes. Never copies, never downloads."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
from typing import TYPE_CHECKING

from cpa_stt import models, template

if TYPE_CHECKING:
    from cpa_stt.cli import Ctx


def _linked(dest: Path, source: Path) -> None:
    target = os.path.realpath(source)
    print(f"linked {dest} -> {target}")
    print(f"note: a link, not a copy: if another app moves or deletes {target}, recordings wait"
          " (model_missing) until you run cpa-stt setup again")  # fmt: skip


def _setup_one(ctx: Ctx, pattern: str, yes: bool) -> None:
    folder = ctx.paths.models
    if models.present(folder, pattern):
        print(f"{pattern}: already in {folder}")
        return
    found = models.in_dirs(pattern, ctx.deps.model_dirs) or models.spotlight(pattern, ctx.deps.run_cmd, folder)
    if not found:
        print(f"{pattern}: not found in {', '.join(map(str, ctx.deps.model_dirs))} or by Spotlight (it skips"
              f" hidden folders such as ~/.cache); give the path: cpa-stt setup /path/to/{pattern}")  # fmt: skip
        return
    others = f" ({len(found) - 1} other match(es) not shown)" if len(found) > 1 else ""
    print(f"{pattern}: found {found[0]} ({os.lstat(found[0]).st_size / 1e6:.1f} MB){others}")
    if yes or ctx.deps.ask(f"link {found[0]} into {folder}? [y/N] ").strip().lower() in ("y", "yes"):
        _linked(models.link(folder, found[0]), found[0])
    else:
        print(f"{pattern}: not linked")


def setup(ctx: Ctx, args: argparse.Namespace) -> int:
    patterns = template.model_patterns(template.example())
    for given in map(Path, args.models):
        if models.matching(given, patterns) is None:
            raise models.ModelError(f"{given.name} is not a model the example uses ({', '.join(patterns)})")
        _linked(models.link(ctx.paths.models, given), given)
    for pattern in () if args.models else patterns:
        _setup_one(ctx, pattern, args.yes)
    print("cpa-stt never downloads models: get them yourself, then link them here")
    return 0
