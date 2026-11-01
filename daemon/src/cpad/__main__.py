"""python -m cpad ... == cpa ... (the LaunchAgent runs `python -m cpad daemon`)."""

import sys

from cpad.cli.main import main

sys.exit(main())
