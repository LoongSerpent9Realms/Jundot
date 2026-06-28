#!/usr/bin/env python3
"""
Clean up remaining Jundot references in build artifacts and generated files.
"""

import os
import shutil
from pathlib import Path

root = Path('H:\\Jundot-Auto')

# Patterns for build artifact directories to clean entirely
BUILD_ARTIFACT_DIRS = [
    'bin/',
    'tools/PackageBuilder/obj/',
    'tools/PackageBuilder/bin/',
]

def should_remove_build_artifact(path):
    """Check if a file is a build artifact that should be removed."""
    path_str = str(path).replace('\\', '/')

    # Remove entire bin/ directory contents (build outputs)
    if '/bin/' in path_str and 'Jundot-Auto/bin/' in path_str:
        return True

    # Remove .NET obj/ and bin/ directories under tools/PackageBuilder
    if 'tools/PackageBuilder/obj/' in path_str:
        return True
    if 'tools/PackageBuilder/bin/' in path_str:
        return True

    return False

def is_text_file(path):
    """Check if file is likely text by extension."""
    text_exts = {
        '.txt', '.json', '.xml', '.cs', '.csproj', '.sln', '.props', '.targets',
        '.cache', '.tres', '.md', '.po', '.pot', '.gd', '.h', '.cpp', '.c',
        '.glsl', '.inc', '.mm', '.java', '.kt', '.py', '.js', '.html', '.css',
        '.bat', '.sh', '.ps1', '.yml', '.yaml', '.toml', '.ini', '.cfg',
        '.iss', '.desktop', '.appdata.xml', '.xsd', '.plist', '.entitlements',
        '.rc', '.svg', '.dotsettings', '.editorconfig', '.sourcelink',
        '.gitignore', '.gitattributes', '.nuspec',
    }
    return path.suffix.lower() in text_exts or path.name in {
        'Makefile', 'SConstruct', 'SCsub', 'CMakeLists.txt',
        '.gitignore', '.gitattributes',
    }

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

def rename_path(path):
    """Rename a file, replacing Jundot->Jundot."""
    name = path.name
    new_name = name
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

print("=" * 60)
print("Step 1: Delete build artifacts with Jundot in name...")
print("=" * 60)

deleted = 0
for dirpath, dirnames, filenames in os.walk(root, topdown=False):
    dirpath_path = Path(dirpath)
    for fname in filenames:
        fpath = dirpath_path / fname
        if 'jundot' in fname.lower() or 'Jundot' in fname or 'JUNDOT' in fname:
            if should_remove_build_artifact(fpath):
                try:
                    fpath.unlink()
                    print(f"  DELETED: {fpath}")
                    deleted += 1
                except Exception as e:
                    print(f"  ERROR deleting {fpath}: {e}")

# Also clean empty directories under bin/
for dirpath, dirnames, filenames in os.walk(root / 'bin', topdown=False):
    dpath = Path(dirpath)
    try:
        if dpath != root / 'bin' and dpath.exists() and not any(dpath.iterdir()):
            dpath.rmdir()
            print(f"  REMOVED EMPTY DIR: {dpath}")
    except:
        pass

print(f"Deleted {deleted} build artifacts.")

print()
print("=" * 60)
print("Step 2: Rename remaining files with Jundot in name...")
print("=" * 60)

all_paths = []
for dirpath, dirnames, filenames in os.walk(root, topdown=False):
    dirpath_path = Path(dirpath)
    for fname in filenames:
        fpath = dirpath_path / fname
        if 'jundot' in fname.lower():
            all_paths.append(fpath)
    for dname in dirnames:
        dpath = dirpath_path / dname
        if 'jundot' in dname.lower():
            all_paths.append(dpath)

all_paths.sort(key=lambda p: len(p.parts), reverse=True)

renamed = 0
for path in all_paths:
    new_path = rename_path(path)
    if new_path != path:
        renamed += 1

print(f"Renamed {renamed} paths.")

print()
print("=" * 60)
print("Step 3: Replace content in remaining text files...")
print("=" * 60)

modified = 0
skipped = 0
for dirpath, dirnames, filenames in os.walk(root, topdown=True):
    dirpath_path = Path(dirpath)
    for fname in filenames:
        fpath = dirpath_path / fname

        # Skip build artifact dirs
        path_str = str(fpath).replace('\\', '/')
        if 'bin/' in path_str and 'Jundot-Auto/bin/' in path_str:
            continue
        if 'tools/PackageBuilder/obj/' in path_str:
            continue
        if 'tools/PackageBuilder/bin/' in path_str:
            continue

        if not is_text_file(fpath):
            skipped += 1
            continue

        if replace_in_file(fpath):
            modified += 1

print(f"Modified {modified} text files.")
print(f"Skipped {skipped} non-text files.")

print()
print("=" * 60)
print("Done!")
print("=" * 60)
