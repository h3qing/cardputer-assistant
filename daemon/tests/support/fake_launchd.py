"""A stand-in for launchctl and the clock (#82): one LaunchAgent in one gui domain, with a bootout that takes
`bootout_delay` seconds of fake time and bootstraps that can refuse while the old job is still loaded (error 5,
as the real one does) or start nothing at all. The real launchctl never runs."""

from __future__ import annotations

from dataclasses import dataclass, field

from cpad.service import launchd

PRINT = """gui/501/{label} = {{
\tactive count = 1
\tpath = /Users/u/Library/LaunchAgents/{label}.plist
\ttype = LaunchAgent
\tstate = {state}
\tprogram = /py
{pid}\tlast exit code = (never exited)
\tendpoints = {{
\t\t"com.example" = {{
\t\t\tstate = active
\t\t}}
\t}}
}}
"""


@dataclass
class FakeLaunchd:
    loaded: bool = False
    pid: int | None = None
    bootout_delay: float = 0.0
    dead_starts: int = 0  # how many bootstraps load the job but never run it
    now: float = 0.0
    calls: list[list[str]] = field(default_factory=list)
    unload_at: float | None = None
    next_pid: int = 4000
    login_env: str = "PATH=/opt/bin:/usr/bin\nHOME=/Users/u\n"

    @property
    def clock(self) -> launchd.Clock:
        return launchd.Clock(sleep=self.sleep, now=lambda: self.now)

    def sleep(self, seconds: float) -> None:
        self.now += seconds

    def _settle(self) -> None:
        if self.unload_at is not None and self.now >= self.unload_at:
            self.loaded, self.pid, self.unload_at = False, None, None

    def __call__(self, argv, **_kw) -> launchd.Completed:
        self.calls.append(list(argv))
        if argv[0] == "/bin/zsh":
            return launchd.Completed(0, self.login_env)
        self._settle()
        verb = argv[1]
        if verb == "print":
            if not self.loaded:
                return launchd.Completed(113, "")
            state, pid = ("running", f"\tpid = {self.pid}\n") if self.pid else ("not running", "")
            return launchd.Completed(0, PRINT.format(label=launchd.LABEL, state=state, pid=pid))
        if verb == "bootout":
            if not self.loaded:
                return launchd.Completed(3, "")
            self.unload_at = self.now + self.bootout_delay
            self._settle()
            return launchd.Completed(0, "")
        if verb == "bootstrap":
            if self.loaded:
                return launchd.Completed(5, "")
            self.loaded = True
            if self.dead_starts:
                self.dead_starts -= 1
            else:
                self.pid, self.next_pid = self.next_pid, self.next_pid + 1
            return launchd.Completed(0, "")
        return launchd.Completed(0, "")

    def verbs(self, verb: str) -> int:
        return sum(1 for c in self.calls if c[:2] == ["launchctl", verb])
