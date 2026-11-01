"""Points the HIL harness (tools/hil) at the simulator instead of the USB port, without changing it.

The harness's Rig lists ports with harness.rig.list_ports and opens them as harness.tap.TapSerial, and the
runner refuses to start while harness.runner.port_holders() finds a Cardputer port held. On the sim:
the lister reports only the pty, TapSerial gets PtySerial's no-op RTS / DTR, and nothing holds a port.
"""

from __future__ import annotations

import contextlib
from collections.abc import Iterator
from typing import Any

from simtools.serial_pty import sim_lister


def sim_patches(path: str) -> list[tuple[Any, str, Any]]:
    """(module, attribute, value) for every harness name to replace."""
    import harness.rig  # noqa: PLC0415 (tools/hil must be on sys.path)
    import harness.runner  # noqa: PLC0415
    import harness.tap  # noqa: PLC0415

    class SimTapSerial(harness.tap.TapSerial):
        def _update_rts_state(self) -> None:
            """A pty has no RTS line."""

        def _update_dtr_state(self) -> None:
            """A pty has no DTR line."""

    return [
        (harness.rig, "list_ports", sim_lister(path)),
        (harness.tap, "TapSerial", SimTapSerial),
        (harness.runner, "port_holders", dict),
    ]


@contextlib.contextmanager
def harness_on_sim(path: str) -> Iterator[None]:
    patches = sim_patches(path)
    saved = [(module, name, getattr(module, name)) for module, name, _ in patches]
    try:
        for module, name, value in patches:
            setattr(module, name, value)
        yield
    finally:
        for module, name, value in saved:
            setattr(module, name, value)
