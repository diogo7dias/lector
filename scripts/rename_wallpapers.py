#!/usr/bin/env python3
"""Rename a Lector wallpaper folder into short uppercase 8.3 names.

WHY
---
On a FAT card a directory is a flat array of 32-byte slots. A file needs one slot,
plus one extra slot for every 13 characters of its long name. So
"beautiful_sunset_over_mountains_042.pxc" (39 characters) costs 4 slots, while
"A3F9.PXC" costs 1.

Lector picks a random wallpaper by seeking to a random directory slot instead of
walking every file. That makes two things follow directly from short names:

  * The directory shrinks (up to ~4x on typical long names), so every read of it
    is cheaper.
  * Every file occupies exactly one slot, so the random pick becomes exactly
    uniform. With mixed name lengths, long-named files cover more slots and are
    picked slightly more often.

A name is stored in a single slot only when it fits classic 8.3 form: 1-8
characters of base name, an optional 1-3 character extension, and UPPERCASE.
Lowercase names can still cost a long-name slot depending on which operating
system wrote them, which is why this script always writes uppercase.

USAGE
-----
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep              # dry run
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep --apply      # rename

Dry run is the default and prints exactly what would happen. Nothing is touched
until --apply is passed.

An undo map is written next to the folder as wallpaper-rename-map.csv (old name,
new name). Pass --undo with that file to put the original names back.

NOTES
-----
  * Renaming is done in two steps through a temporary name, because FAT is
    case-insensitive: renaming "abc.pxc" straight to "ABC.PXC" is a no-op or an
    error on macOS and Windows.
  * Files already in the target form are left alone, so re-running is safe.
  * macOS metadata (.DS_Store, ._AppleDouble) is skipped, never renamed. Deleting
    those is a separate decision and this script does not make it for you.
  * Subfolders are not touched and are not descended into.
"""

import argparse
import csv
import os
import random
import re
import string
import sys

# Extensions Lector accepts as a sleep wallpaper. Case-insensitive on input; always
# written uppercase.
WALLPAPER_EXTS = {".pxc", ".bmp"}

# Base name alphabet. Digits and uppercase letters only — every character here is
# valid in an 8.3 short name on every FAT implementation.
ALPHABET = string.digits + string.ascii_uppercase

UNDO_FILENAME = "wallpaper-rename-map.csv"

# Already-good: 1-8 base characters, a dot, 1-3 extension characters, all uppercase.
GOOD_NAME = re.compile(r"^[0-9A-Z]{1,8}\.[0-9A-Z]{1,3}$")


def is_metadata(name):
    return name == ".DS_Store" or name.startswith("._") or name.startswith(".")


def wallpapers_in(folder):
    """Wallpaper file names in `folder`, sorted, excluding metadata and subfolders."""
    names = []
    for name in os.listdir(folder):
        if is_metadata(name):
            continue
        if not os.path.isfile(os.path.join(folder, name)):
            continue
        if os.path.splitext(name)[1].lower() not in WALLPAPER_EXTS:
            continue
        names.append(name)
    return sorted(names)


def name_length_for(count, length_arg):
    """Base-name length to use. `auto` picks the shortest length that leaves plenty of
    headroom, so collisions stay rare and the loop below terminates quickly."""
    if length_arg != "auto":
        return int(length_arg)
    for length in range(2, 9):
        if len(ALPHABET) ** length >= count * 20:
            return length
    return 8


def build_plan(folder, names, length, rng):
    """List of (old, new) pairs. Skips files already in the target form."""
    taken = {n.upper() for n in names}
    plan = []
    for old in names:
        if GOOD_NAME.match(old):
            continue
        ext = os.path.splitext(old)[1].upper()
        while True:
            base = "".join(rng.choice(ALPHABET) for _ in range(length))
            candidate = base + ext
            if candidate not in taken:
                break
        taken.add(candidate)
        plan.append((old, candidate))
    return plan


def apply_plan(folder, plan):
    """Rename via a temporary name so case-only changes work on case-insensitive
    filesystems. Returns the pairs that actually completed."""
    done = []
    # The undo map is written and flushed as each rename lands, never at the end. A run
    # over thousands of files takes minutes, and a batch written only on completion would
    # leave every already-renamed file unrecoverable if the run were interrupted or the
    # card pulled.
    map_path = undo_map_path(folder)
    exists = os.path.exists(map_path)
    with open(map_path, "a", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        if not exists:
            writer.writerow(["original_name", "new_name"])
            handle.flush()
        total = len(plan)
        for index, (old, new) in enumerate(plan):
            old_path = os.path.join(folder, old)
            new_path = os.path.join(folder, new)
            tmp_path = os.path.join(folder, "~RN%06d.TMP" % index)
            try:
                os.rename(old_path, tmp_path)
                os.rename(tmp_path, new_path)
            except OSError as err:
                print("  FAILED %s -> %s: %s" % (old, new, err), file=sys.stderr)
                if os.path.exists(tmp_path):
                    os.rename(tmp_path, old_path)
                continue
            writer.writerow([old, new])
            handle.flush()
            os.fsync(handle.fileno())
            done.append((old, new))
            if total >= 200 and (index + 1) % 100 == 0:
                print("  %d / %d" % (index + 1, total), flush=True)
    return done


def undo_map_path(folder):
    return os.path.join(os.path.dirname(os.path.abspath(folder)), UNDO_FILENAME)


def run_undo(folder, map_path, apply_changes):
    with open(map_path, newline="", encoding="utf-8") as handle:
        rows = [row for row in csv.reader(handle)][1:]
    plan = [(new, old) for old, new in rows if os.path.exists(os.path.join(folder, new))]
    print("Undo: %d of %d mapped files still present." % (len(plan), len(rows)))
    for new, old in plan[:10]:
        print("  %s -> %s" % (new, old))
    if len(plan) > 10:
        print("  ... and %d more" % (len(plan) - 10))
    if not apply_changes:
        print("\nDry run. Re-run with --apply to restore these names.")
        return 0
    done = apply_plan(folder, plan)
    print("\nRestored %d of %d." % (len(done), len(plan)))
    return 0 if len(done) == len(plan) else 1


def main():
    parser = argparse.ArgumentParser(description="Rename Lector wallpapers to short uppercase 8.3 names.")
    parser.add_argument("folder", help="wallpaper folder, e.g. /Volumes/LECTOR/sleep")
    parser.add_argument("--apply", action="store_true", help="actually rename (default is a dry run)")
    parser.add_argument("--length", default="auto", help="base-name length, 2-8, or 'auto' (default)")
    parser.add_argument("--seed", type=int, default=None, help="random seed, for a reproducible run")
    parser.add_argument("--undo", metavar="MAP_CSV", help="restore original names from a rename map")
    args = parser.parse_args()

    folder = os.path.abspath(os.path.expanduser(args.folder))
    if not os.path.isdir(folder):
        print("Not a folder: %s" % folder, file=sys.stderr)
        return 2

    if args.undo:
        return run_undo(folder, os.path.abspath(os.path.expanduser(args.undo)), args.apply)

    names = wallpapers_in(folder)
    if not names:
        print("No .pxc or .bmp files in %s" % folder)
        return 0

    length = name_length_for(len(names), args.length)
    rng = random.Random(args.seed)
    plan = build_plan(folder, names, length, rng)

    slots_before = sum(1 + (len(n) + 12) // 13 for n in names)
    slots_after = len(names)

    print("Folder:      %s" % folder)
    print("Wallpapers:  %d" % len(names))
    print("Already 8.3: %d" % (len(names) - len(plan)))
    print("To rename:   %d  (base-name length %d)" % (len(plan), length))
    print("Directory:   ~%d slots -> ~%d slots  (%d KB -> %d KB)"
          % (slots_before, slots_after, slots_before * 32 // 1024, slots_after * 32 // 1024))

    if not plan:
        print("\nNothing to do.")
        return 0

    print("\nExamples:")
    for old, new in plan[:10]:
        print("  %s -> %s" % (old, new))
    if len(plan) > 10:
        print("  ... and %d more" % (len(plan) - 10))

    if not args.apply:
        print("\nDry run. Nothing was changed. Re-run with --apply to rename.")
        return 0

    print("\nRenaming...")
    done = apply_plan(folder, plan)
    map_path = undo_map_path(folder) if done else None
    print("Renamed %d of %d." % (len(done), len(plan)))
    if map_path:
        print("Undo map: %s" % map_path)
        print("Restore with: python3 %s %s --undo %s --apply" % (sys.argv[0], folder, map_path))
    return 0 if len(done) == len(plan) else 1


if __name__ == "__main__":
    sys.exit(main())
