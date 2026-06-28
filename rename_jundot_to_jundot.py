#!/usr/bin/env python3
"""
Rename all Jundot references to Jundot in the project.
Handles:
- File/directory names: Jundot -> Jundot, jundot -> jundot, JUNDOT -> JUNDOT
- File contents: same replacements, but only in text files
"""

import os
import sys
from pathlib import Path

# Binary file extensions to skip content replacement
BINARY_EXTENSIONS = {
    '.dll', '.pdb', '.obj', '.lib', '.exe', '.bin', '.ico', '.icns',
    '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.svg', '.dds', '.ktx',
    '.ttf', '.otf', '.woff', '.woff2',
    '.mp3', '.ogg', '.wav', '.flac',
    '.mp4', '.webm', '.avi',
    '.zip', '.tar', '.gz', '.bz2', '.7z', '.rar',
    '.db', '.sqlite', '.sqlite3',
    '.pdf', '.doc', '.docx', '.xls', '.xlsx', '.ppt', '.pptx',
    '.so', '.dylib', '.a', '.o',
    '.class', '.jar',
    '.etg', '.nupkg', '.cache',
    '.resources',  # Compiled .NET resources
}

# Build artifacts / generated files to skip entirely
SKIP_PATHS = [
    '__pycache__',
    '.git',
    'bin/',  # Build output directory
    'tools/PackageBuilder/obj/',  # .NET build artifacts
]

def should_skip_path(path):
    """Check if a path should be skipped entirely."""
    path_str = str(path).replace('\\', '/')
    for skip in SKIP_PATHS:
        if skip in path_str:
            return True
    return False

def is_binary_file(path):
    """Check if file is binary by extension."""
    return path.suffix.lower() in BINARY_EXTENSIONS

def rename_file_or_dir(path):
    """Rename a file or directory, replacing Jundot->Jundot case-insensitively."""
    name = path.name
    new_name = name

    # Replace all case variants
    new_name = new_name.replace('Jundot', 'Jundot')
    new_name = new_name.replace('jundot', 'jundot')
    new_name = new_name.replace('JUNDOT', 'JUNDOT')

    if new_name != name:
        new_path = path.parent / new_name
        try:
            path.rename(new_path)
            print(f"  RENAMED: {path} -> {new_path}")
            return new_path
        except Exception as e:
            print(f"  ERROR renaming {path}: {e}")
            return path
    return path

def replace_in_file(path):
    """Replace Jundot->Jundot in file contents."""
    try:
        with open(path, 'r', encoding='utf-8', errors='strict') as f:
            content = f.read()
    except (UnicodeDecodeError, PermissionError, IsADirectoryError):
        return False
    except Exception as e:
        print(f"  ERROR reading {path}: {e}")
        return False

    new_content = content
    new_content = new_content.replace('Jundot', 'Jundot')
    new_content = new_content.replace('jundot', 'jundot')
    new_content = new_content.replace('JUNDOT', 'JUNDOT')

    if new_content != content:
        try:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(new_content)
            print(f"  MODIFIED: {path}")
            return True
        except Exception as e:
            print(f"  ERROR writing {path}: {e}")
            return False
    return False

def main():
    root = Path('H:\\Jundot-Auto')

    print("=" * 60)
    print("Step 1: Collecting all files and directories to rename...")
    print("=" * 60)

    # Collect all paths that need renaming (deepest first to avoid conflicts)
    all_paths = []
    for dirpath, dirnames, filenames in os.walk(root, topdown=False):
        dirpath_path = Path(dirpath)

        if should_skip_path(dirpath_path):
            continue

        # Add files
        for fname in filenames:
            fpath = dirpath_path / fname
            if should_skip_path(fpath):
                continue
            if 'jundot' in fname.lower():
                all_paths.append(fpath)

        # Add directories
        for dname in dirnames:
            dpath = dirpath_path / dname
            if should_skip_path(dpath):
                continue
            if 'jundot' in dname.lower():
                all_paths.append(dpath)

    # Sort by depth (deepest first) so we rename children before parents
    all_paths.sort(key=lambda p: len(p.parts), reverse=True)

    print(f"Found {len(all_paths)} paths to rename.")

    print()
    print("=" * 60)
    print("Step 2: Renaming files and directories...")
    print("=" * 60)

    renamed_count = 0
    path_mapping = {}  # old -> new

    for path in all_paths:
        new_path = rename_file_or_dir(path)
        if new_path != path:
            renamed_count += 1
            path_mapping[str(path)] = str(new_path)

    print(f"Renamed {renamed_count} paths.")

    print()
    print("=" * 60)
    print("Step 3: Replacing content in all text files...")
    print("=" * 60)

    modified_count = 0
    skipped_binary = 0
    error_count = 0

    for dirpath, dirnames, filenames in os.walk(root, topdown=True):
        dirpath_path = Path(dirpath)

        if should_skip_path(dirpath_path):
            continue

        for fname in filenames:
            fpath = dirpath_path / fname

            if should_skip_path(fpath):
                continue

            if is_binary_file(fpath):
                skipped_binary += 1
                continue

            if replace_in_file(fpath):
                modified_count += 1

    print(f"Modified {modified_count} text files.")
    print(f"Skipped {skipped_binary} binary files.")
    print(f"Encountered {error_count} errors.")

    print()
    print("=" * 60)
    print("Done!")
    print("=" * 60)

if __name__ == '__main__':
    main()
