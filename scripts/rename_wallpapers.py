#!/usr/bin/env python3
"""Rename a Lector wallpaper folder into short 8.3 names.

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
characters of base name and an optional 1-3 character extension. UPPERCASE is the
default because it is the only form guaranteed to stay in one slot everywhere.
Pass --lowercase for names like "a3f9.pxc": those rely on the FAT lowercase flags,
which some systems ignore, storing a long-name slot instead.

USAGE
-----
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep                        # dry run
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep --apply                # rename
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep --lowercase --apply    # a3f9.pxc
    python3 rename_wallpapers.py /Volumes/LECTOR/sleep --compact --apply      # shrink the directory

Renaming alone does not shrink the directory: FAT only marks the old long-name
slots deleted and never shortens the directory itself. Run --compact afterwards to
move every file into a freshly created folder, which writes a tight directory.

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

# Extensions Lector accepts as a sleep wallpaper. Matched case-insensitively on input;
# written uppercase by default, lowercase with --lowercase. The firmware compares
# extensions case-insensitively either way.
WALLPAPER_EXTS = {".pxc", ".bmp"}

# Base name alphabet. Digits and uppercase letters only — every character here is
# valid in an 8.3 short name on every FAT implementation.
ALPHABET = string.digits + string.ascii_uppercase

UNDO_FILENAME = "wallpaper-rename-map.csv"

# Already-good: 1-8 base characters, a dot, 1-3 extension characters, single case.
GOOD_NAME_UPPER = re.compile(r"^[0-9A-Z]{1,8}\.[0-9A-Z]{1,3}$")
GOOD_NAME_LOWER = re.compile(r"^[0-9a-z]{1,8}\.[0-9a-z]{1,3}$")


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


def build_plan(folder, names, length, rng, lowercase=False):
    """List of (old, new) pairs. Skips files already in the target form."""
    good = GOOD_NAME_LOWER if lowercase else GOOD_NAME_UPPER
    alphabet = ALPHABET.lower() if lowercase else ALPHABET
    # Compared case-folded: FAT cannot tell "A3F9.PXC" from "a3f9.pxc", so a name that
    # differs only in case is still a collision.
    taken = {n.upper() for n in names}
    plan = []
    for old in names:
        if good.match(old):
            continue
        ext = os.path.splitext(old)[1]
        ext = ext.lower() if lowercase else ext.upper()
        while True:
            base = "".join(rng.choice(alphabet) for _ in range(length))
            candidate = base + ext
            if candidate.upper() not in taken:
                break
        taken.add(candidate.upper())
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


def run_compact(folder, apply_changes):
    """Rebuild the folder so its directory holds only live slots.

    FAT never shrinks a directory. Renaming long names to short ones only marks the old
    long-name slots deleted; the directory keeps its length and stays mostly dead slots.
    That costs reads on every scan, and it re-introduces the fairness tilt for a picker
    that lands on a random slot: a file sitting after a long run of dead slots gets
    chosen more often.

    Moving every file into a freshly created folder writes a new, tight directory.
    """
    parent = os.path.dirname(folder)
    base = os.path.basename(folder)
    staging = os.path.join(parent, base + "_compact")

    entries = sorted(os.listdir(folder))
    subdirs = [e for e in entries if os.path.isdir(os.path.join(folder, e))]
    files = [e for e in entries if not os.path.isdir(os.path.join(folder, e))]
    live_slots = sum(1 + (len(n) + 12) // 13 for n in files)

    print("Folder:     %s" % folder)
    print("Files:      %d" % len(files))
    print("Subfolders: %d" % len(subdirs))
    print("Rebuilt directory: ~%d slots (~%d KB)" % (live_slots, live_slots * 32 // 1024))
    print("\nPlan:")
    print("  1. create %s" % staging)
    print("  2. move all %d files into it" % len(files))
    print("  3. remove the empty %s" % folder)
    print("  4. rename %s back to %s" % (staging, base))

    if subdirs:
        print("\nRefusing: %s contains subfolders (%s). Move them out first."
              % (folder, ", ".join(subdirs[:5])), file=sys.stderr)
        return 2
    if os.path.exists(staging):
        print("\nRefusing: %s already exists. Remove it first." % staging, file=sys.stderr)
        return 2
    if not files:
        print("\nNothing to move.")
        return 0
    if not apply_changes:
        print("\nDry run. Nothing was changed. Re-run with --apply to compact.")
        return 0

    print("\nCompacting...")
    os.mkdir(staging)
    moved = 0
    for name in files:
        try:
            os.rename(os.path.join(folder, name), os.path.join(staging, name))
        except OSError as err:
            print("  FAILED to move %s: %s" % (name, err), file=sys.stderr)
            continue
        moved += 1
        if len(files) >= 200 and moved % 100 == 0:
            print("  %d / %d" % (moved, len(files)), flush=True)

    remaining = os.listdir(folder)
    if remaining:
        print("\n%d file(s) could not be moved; leaving both folders in place." % len(remaining), file=sys.stderr)
        print("Original: %s\nStaging:  %s" % (folder, staging), file=sys.stderr)
        return 1

    os.rmdir(folder)
    os.rename(staging, folder)
    print("\nMoved %d files. %s now has a fresh directory." % (moved, folder))
    return 0


def main():
    parser = argparse.ArgumentParser(description="Rename Lector wallpapers to short uppercase 8.3 names.")
    parser.add_argument("folder", help="wallpaper folder, e.g. /Volumes/LECTOR/sleep")
    parser.add_argument("--apply", action="store_true", help="actually rename (default is a dry run)")
    parser.add_argument("--length", default="auto", help="base-name length, 2-8, or 'auto' (default)")
    parser.add_argument("--seed", type=int, default=None, help="random seed, for a reproducible run")
    parser.add_argument("--undo", metavar="MAP_CSV", help="restore original names from a rename map")
    parser.add_argument("--lowercase", action="store_true",
                        help="write lowercase names (a3f9.pxc) instead of uppercase (A3F9.PXC). "
                             "Uppercase is the only form guaranteed to occupy a single directory "
                             "slot on every system; lowercase relies on the FAT lowercase flags, "
                             "which some systems ignore and store a long-name slot instead")
    parser.add_argument("--compact", action="store_true",
                        help="rebuild the folder so its directory drops the dead slots left by renaming")
    args = parser.parse_args()

    folder = os.path.abspath(os.path.expanduser(args.folder))
    if not os.path.isdir(folder):
        print("Not a folder: %s" % folder, file=sys.stderr)
        return 2

    if args.compact:
        return run_compact(folder, args.apply)

    if args.undo:
        return run_undo(folder, os.path.abspath(os.path.expanduser(args.undo)), args.apply)

    names = wallpapers_in(folder)
    if not names:
        print("No .pxc or .bmp files in %s" % folder)
        return 0

    length = name_length_for(len(names), args.length)
    rng = random.Random(args.seed)
    plan = build_plan(folder, names, length, rng, args.lowercase)

    slots_before = sum(1 + (len(n) + 12) // 13 for n in names)
    slots_after = len(names)

    print("Folder:      %s" % folder)
    print("Wallpapers:  %d" % len(names))
    print("Already 8.3: %d" % (len(names) - len(plan)))
    print("To rename:   %d  (base-name length %d, %s)"
          % (len(plan), length, "lowercase" if args.lowercase else "uppercase"))
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
