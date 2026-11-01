"""cpa-stt: the transcriber role of recording contract v1 (docs/22 §4), a separate program next to cpa.

It finds finished recordings, runs the user's configured local command on them inside a sandbox and writes
status.json, transcript.json, transcript.md and result.json. cpad keeps no transcription code and never
imports this package; cpa-stt writes only into the recording folders and reads cpad's config.json and
projects.json (ADR 0004, docs/22 §4.13).
"""

APP = "cpa-stt"
