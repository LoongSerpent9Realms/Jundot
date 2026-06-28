#!/usr/bin/env python

import argparse
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_DIR = ROOT / "modules" / "wwise"
DEFAULT_OUTPUT = ROOT / "artifacts" / "wwise-module"


def copy_module(staging_root: Path) -> Path:
    module_target = staging_root / "wwise"
    if module_target.exists():
        shutil.rmtree(module_target)
    shutil.copytree(
        MODULE_DIR,
        module_target,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", ".sconsign*", "*.obj", "*.lib", "*.dll", "*.pdb"),
    )
    return module_target


def write_package_readme(staging_root: Path) -> None:
    (staging_root / "README.md").write_text(
        """# Jundot Wwise module

This package contains the optional `wwise` custom module for Jundot/Godot.

It does not include the Audiokinetic Wwise SDK. Install Wwise separately, then build with your local SDK path:

```powershell
python -m SCons platform=windows target=editor custom_modules="C:\\path\\to\\custom_modules" module_wwise_enabled=yes wwise_sdk_path="F:\\Wwise_2025.1.8.9170\\SDK" wwise_config=Profile
```

Expected layout after extraction:

```text
custom_modules/
  wwise/
    SCsub
    config.py
    register_types.*
    wwise.*
```

The module exposes a `Wwise` singleton with initialization, bank loading, game-object registration, event posting, RTPC, and per-frame audio rendering helpers.

The first package links the Wwise foundation libraries (`AkSoundEngine`, `AkMemoryMgr`, and `AkStreamMgr`). Add-on libraries such as Spatial Audio can be layered on top once the base bridge is compiling in the target engine.
""",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Package the optional Wwise custom module.")
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT), help="Directory where the package will be written.")
    parser.add_argument("--name", default="jundot-wwise-module", help="Base package name.")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    staging_root = output_dir / args.name
    if staging_root.exists():
        shutil.rmtree(staging_root)
    staging_root.mkdir(parents=True)

    copy_module(staging_root)
    write_package_readme(staging_root)

    archive_base = output_dir / args.name
    archive_path = shutil.make_archive(str(archive_base), "zip", root_dir=staging_root)
    print(archive_path)


if __name__ == "__main__":
    main()
