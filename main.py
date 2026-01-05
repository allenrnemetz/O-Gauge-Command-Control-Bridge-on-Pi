#!/usr/bin/env python3
"""
main.py - Entry point for Arduino App Lab

This is the main entry point when running on Arduino UNO Q via App Lab.
It simply imports and runs the bridge.

For direct SSH execution, you can also run:
    python3 lionel_mth_bridge.py
"""

from lionel_mth_bridge import main

if __name__ == "__main__":
    main()
