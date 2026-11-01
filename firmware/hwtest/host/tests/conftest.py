import os
import sys

# The host scripts are plain modules next to this folder (no package).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
