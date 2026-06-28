#!/usr/bin/env python

import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "artifacts" / "wwise-addon"
PACKAGE_NAME = "jundot-wwise-addon"

FILES = [
    ("addons/jundot_wwise/plugin.cfg", "addons/jundot_wwise/plugin.cfg"),
    ("addons/jundot_wwise/wwise_plugin.gd", "addons/jundot_wwise/wwise_plugin.gd"),
    ("addons/jundot_wwise/wwise_runtime.gd", "addons/jundot_wwise/wwise_runtime.gd"),
    ("addons/jundot_wwise/README.md", "addons/jundot_wwise/README.md"),
    ("Audio/GeneratedSoundBanks/Windows/.gdignore", "Audio/GeneratedSoundBanks/Windows/.gdignore"),
]


def main() -> None:
    staging = OUTPUT_DIR / PACKAGE_NAME
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    for source, dest in FILES:
        target = staging / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, target)

    archive = shutil.make_archive(str(OUTPUT_DIR / PACKAGE_NAME), "zip", root_dir=staging)
    print(archive)


if __name__ == "__main__":
    main()
