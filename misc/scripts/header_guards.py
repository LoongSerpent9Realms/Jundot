#!/usr/bin/env python3

import sys


def is_supported_jundot_header(lines, start):
    if start >= len(lines):
        return False

    first = lines[start].strip()
    if not (
        first.startswith("/**********")
        or (
            first.startswith("/*  ")
            and first.endswith("*/")
            and "This file is part of:" not in first
            and start + 1 < len(lines)
            and lines[start + 1].strip().startswith("/**********")
        )
    ):
        return False

    header_preview = "".join(lines[start : start + 40])
    return "This file is part of:" in header_preview and (
        "GODOT ENGINE" in header_preview or "JUNDOT ENGINE" in header_preview or "JunDot" in header_preview
    )


if len(sys.argv) < 2:
    print("Invalid usage of header_guards.py, it should be called with a path to one or multiple files.")
    sys.exit(1)

changed = []
invalid = []

for file in sys.argv[1:]:
    with open(file.strip(), "rt", encoding="utf-8", newline="\n") as f:
        lines = f.readlines()

    header_start = 0
    while header_start < len(lines) and lines[header_start].strip() == "":
        header_start += 1

    if is_supported_jundot_header(lines, header_start):
        guard_offset = header_start
        while guard_offset < len(lines) and lines[guard_offset].strip().startswith(("/*", "*")):
            guard_offset += 1
        while guard_offset < len(lines) and lines[guard_offset].strip() == "":
            guard_offset += 1
    else:
        guard_offset = header_start

    if (HEADER_CHECK_OFFSET := guard_offset) < 0 or HEADER_CHECK_OFFSET >= len(lines):
        invalid.append(file)
        continue

    if lines[HEADER_CHECK_OFFSET].startswith("#pragma once"):
        continue

    # Might be using legacy header guards.
    HEADER_BEGIN_OFFSET = HEADER_CHECK_OFFSET + 1
    HEADER_END_OFFSET = len(lines) - 1

    if HEADER_BEGIN_OFFSET >= HEADER_END_OFFSET:
        invalid.append(file)
        continue

    if (
        lines[HEADER_CHECK_OFFSET].startswith("#ifndef")
        and lines[HEADER_BEGIN_OFFSET].startswith("#define")
        and lines[HEADER_END_OFFSET].startswith("#endif")
    ):
        lines[HEADER_CHECK_OFFSET] = "#pragma once"
        lines[HEADER_BEGIN_OFFSET] = "\n"
        lines.pop()
        with open(file, "wt", encoding="utf-8", newline="\n") as f:
            f.writelines(lines)
        changed.append(file)
        continue

    # Verify `#pragma once` doesn't exist at invalid location.
    misplaced = False
    for line in lines:
        if line.startswith("#pragma once"):
            misplaced = True
            break

    if misplaced:
        invalid.append(file)
        continue

    # Assume that we're simply missing a guard entirely.
    lines.insert(HEADER_CHECK_OFFSET, "#pragma once\n\n")
    with open(file, "wt", encoding="utf-8", newline="\n") as f:
        f.writelines(lines)
    changed.append(file)

if changed:
    for file in changed:
        print(f"FIXED: {file}")
if invalid:
    for file in invalid:
        print(f"REQUIRES MANUAL CHANGES: {file}")
    sys.exit(1)
