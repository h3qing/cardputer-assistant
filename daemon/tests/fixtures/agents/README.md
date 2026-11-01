Redacted `claude agents --json` / `--all` output from Claude Code 2.1.272 (docs/gates/cli-gates.md §2, §5).
Paths and names are replaced; field names, kinds (`interactive`, `background`, registry form `bg`,
`daemon`, `daemon-worker`), statuses and states are as observed or as found in the 2.1.272 binary.
One status (`thinking`) and one kind (`mystery`) are invented to test lenient parsing.

`agents-2.1.306.json`: rows in the Claude Code 2.1.306 shapes (#5, docs/gates/cli-gates.md §5a): `status: waiting`
with each `waitingFor` value (one invented value to test lenient parsing), background `state` blocked / done /
working, and a background `idle` + `blocked` row for a failed (logged-out) turn. Built from the binary's
`printAgentsJson` and one read-only listing; ids and paths are made up.
