"""python -m cpa_stt ... == cpa-stt ... (the LaunchAgent runs `python -m cpa_stt watch`)."""

import sys

from cpa_stt.cli import main

sys.exit(main())
