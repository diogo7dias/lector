#!/usr/bin/env python3
"""Prepare a Lector wallpaper folder for fast random picking, in one command.

    python3 rename_wallpapers.py /Volumes/CARD/sleep            # dry run, shows the plan
    python3 rename_wallpapers.py /Volumes/CARD/sleep --apply    # do it

Three steps run in order, and each one is worth doing on its own:

  1. STRIP    delete the macOS "._NAME" companions and .DS_Store.
              macOS writes one hidden companion per file on a FAT card to carry
              extended attributes. Lector never reads them, and the leading dot
              breaks the 8.3 rule so each costs two directory slots — on a real
              4172-wallpaper card they doubled the directory all by themselves.

  2. RENAME   give every wallpaper a short 8.3 name such as A3F9.PXC.
              A FAT directory is a flat array of 32-byte slots. A file takes one
              slot, plus one more for every 13 characters of long name, so a
              100-character generated filename costs about nine. Short names cut
              that to one.

  3. COMPACT  move everything into a freshly created folder.
              FAT never shrinks a directory: renaming only marks the old
              long-name slots deleted, and the directory keeps its full length.
              Only a new folder gets a tight directory.

Why it matters: Lector picks a random wallpaper by seeking to a random directory
slot rather than walking every file. A small directory makes that seek cheap, and
one slot per file makes the pick exactly uniform — with mixed name lengths, or
with dead slots left over from renaming, files that sit after a long run of slots
get chosen more often.

Subtract a step when you want to:

    --no-strip      keep the macOS metadata files
    --no-rename     leave filenames alone
    --no-compact    skip the folder rebuild
    --lowercase     write a3f9.pxc instead of A3F9.PXC. Uppercase is the default
                    because it is the only form guaranteed to stay in a single
                    slot everywhere; lowercase relies on the FAT lowercase flags,
                    which some systems ignore and store a long-name slot for
                    instead. The firmware matches either.

Undo: every rename is appended to wallpaper-rename-map.csv next to the folder as
it happens, so an interrupted run still leaves a complete record.

    python3 rename_wallpapers.py /Volumes/CARD/sleep --undo /Volumes/CARD/wallpaper-rename-map.csv --apply

Notes: renaming goes through a temporary name because FAT cannot tell "abc.pxc"
from "ABC.PXC"; files already in the target form are skipped so re-running is
safe; a folder containing subfolders is never compacted.
"""

import argparse
import csv
import os
import random
import re
import string
import sys

# Extensions Lector accepts as a sleep wallpaper. Matched case-insensitively.
WALLPAPER_EXTS = {".pxc", ".bmp"}

# Base-name alphabet. Digits and letters only — every character here is valid in an 8.3
# short name on every FAT implementation.
ALPHABET = string.digits + string.ascii_uppercase

UNDO_FILENAME = "wallpaper-rename-map.csv"

# 1-8 base characters, a dot, 1-3 extension characters, all one case.
GOOD_NAME_UPPER = re.compile(r"^[0-9A-Z]{1,8}\.[0-9A-Z]{1,3}$")
GOOD_NAME_LOWER = re.compile(r"^[0-9a-z]{1,8}\.[0-9a-z]{1,3}$")


# --------------------------------------------------------------------------- helpers


def is_metadata(name):
    """macOS bookkeeping that the reader never opens."""
    return name == ".DS_Store" or name.startswith("._") or name.startswith(".")


def is_wallpaper(name):
    return not is_metadata(name) and os.path.splitext(name)[1].lower() in WALLPAPER_EXTS


def slots_for(name):
    """Directory slots a name occupies: one short entry, plus one long-name entry per 13
    characters when the name does not already fit classic 8.3 form."""
    if GOOD_NAME_UPPER.match(name) or GOOD_NAME_LOWER.match(name):
        return 1
    return 1 + (len(name) + 12) // 13


def scan(folder):
    """(files, subdirs) in `folder`, both sorted."""
    files, subdirs = [], []
    for name in sorted(os.listdir(folder)):
        (subdirs if os.path.isdir(os.path.join(folder, name)) else files).append(name)
    return files, subdirs


def undo_map_path(folder):
    return os.path.join(os.path.dirname(os.path.abspath(folder)), UNDO_FILENAME)


def name_length_for(count, length_arg):
    """Base-name length. `auto` picks the shortest length with enough headroom that
    collisions stay rare and the search loop terminates quickly."""
    if length_arg != "auto":
        return int(length_arg)
    for length in range(2, 9):
        if len(ALPHABET) ** length >= max(count, 1) * 20:
            return length
    return 8


def progress(done, total, label):
    if total >= 200 and done % 100 == 0:
        print("      %s %d / %d" % (label, done, total), flush=True)


# ----------------------------------------------------------------------------- steps


def build_rename_plan(names, length, rng, lowercase):
    """(old, new) pairs. Names already in the target form are skipped."""
    good = GOOD_NAME_LOWER if lowercase else GOOD_NAME_UPPER
    alphabet = ALPHABET.lower() if lowercase else ALPHABET
    # Case-folded: FAT cannot tell "A3F9.PXC" from "a3f9.pxc", so a name differing only
    # in case is still a collision.
    taken = {n.upper() for n in names}
    plan = []
    for old in names:
        if good.match(old):
            continue
        ext = os.path.splitext(old)[1]
        ext = ext.lower() if lowercase else ext.upper()
        while True:
            candidate = "".join(rng.choice(alphabet) for _ in range(length)) + ext
            if candidate.upper() not in taken:
                break
        taken.add(candidate.upper())
        plan.append((old, candidate))
    return plan


def do_strip(folder, junk):
    removed = 0
    for name in junk:
        try:
            os.remove(os.path.join(folder, name))
        except OSError as err:
            print("      FAILED to delete %s: %s" % (name, err), file=sys.stderr)
            continue
        removed += 1
        progress(removed, len(junk), "deleted")
    return removed


def do_rename(folder, plan):
    """Rename through a temporary name so case-only changes work on case-insensitive
    filesystems. The undo map is appended and fsynced per file, never batched at the
    end: a run over thousands of files takes minutes, and an interrupted batch would
    leave every already-renamed file with no record of its original name."""
    done = []
    map_path = undo_map_path(folder)
    exists = os.path.exists(map_path)
    with open(map_path, "a", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        if not exists:
            writer.writerow(["original_name", "new_name"])
            handle.flush()
        for index, (old, new) in enumerate(plan):
            tmp_path = os.path.join(folder, "~RN%06d.TMP" % index)
            try:
                os.rename(os.path.join(folder, old), tmp_path)
                os.rename(tmp_path, os.path.join(folder, new))
            except OSError as err:
                print("      FAILED %s -> %s: %s" % (old, new, err), file=sys.stderr)
                if os.path.exists(tmp_path):
                    os.rename(tmp_path, os.path.join(folder, old))
                continue
            writer.writerow([old, new])
            handle.flush()
            os.fsync(handle.fileno())
            done.append((old, new))
            progress(len(done), len(plan), "renamed")
    return done


def do_compact(folder):
    """Move every file into a freshly created folder, then swap it into place."""
    parent, base = os.path.dirname(folder), os.path.basename(folder)
    staging = os.path.join(parent, base + "_compact")
    if os.path.exists(staging):
        print("      Refusing: %s already exists. Remove it first." % staging, file=sys.stderr)
        return 0
    files = [n for n in os.listdir(folder) if not os.path.isdir(os.path.join(folder, n))]

    os.mkdir(staging)
    moved = 0
    for name in files:
        try:
            os.rename(os.path.join(folder, name), os.path.join(staging, name))
        except OSError as err:
            print("      FAILED to move %s: %s" % (name, err), file=sys.stderr)
            continue
        moved += 1
        progress(moved, len(files), "moved")

    remaining = os.listdir(folder)
    if remaining:
        print("      %d item(s) left behind; keeping both folders." % len(remaining), file=sys.stderr)
        print("      Original: %s\n      Staging:  %s" % (folder, staging), file=sys.stderr)
        return moved
    os.rmdir(folder)
    os.rename(staging, folder)
    return moved


# ------------------------------------------------------------------------------ undo


def run_undo(folder, map_path, apply_changes):
    with open(map_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.reader(handle))[1:]
    plan = [(new, old) for old, new in rows if os.path.exists(os.path.join(folder, new))]
    print("Undo: %d of %d mapped files still present." % (len(plan), len(rows)))
    for new, old in plan[:10]:
        print("  %s -> %s" % (new, old))
    if len(plan) > 10:
        print("  ... and %d more" % (len(plan) - 10))
    if not apply_changes:
        print("\nDry run. Re-run with --apply to restore these names.")
        return 0
    done = do_rename(folder, plan)
    print("\nRestored %d of %d." % (len(done), len(plan)))
    return 0 if len(done) == len(plan) else 1


# ------------------------------------------------------------------------------ main


def main():
    parser = argparse.ArgumentParser(
        description="Strip macOS metadata, shorten wallpaper names, and rebuild the folder.")
    parser.add_argument("folder", help="wallpaper folder, e.g. /Volumes/CARD/sleep")
    parser.add_argument("--apply", action="store_true", help="do it (default is a dry run)")
    parser.add_argument("--no-strip", action="store_true", help="keep the macOS metadata files")
    parser.add_argument("--no-rename", action="store_true", help="leave filenames alone")
    parser.add_argument("--no-compact", action="store_true", help="skip the folder rebuild")
    parser.add_argument("--lowercase", action="store_true", help="write a3f9.pxc instead of A3F9.PXC")
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

    files, subdirs = scan(folder)
    junk = [n for n in files if is_metadata(n)]
    wallpapers = [n for n in files if is_wallpaper(n)]
    others = [n for n in files if not is_metadata(n) and not is_wallpaper(n)]

    if not files:
        print("Nothing to do: %s is empty" % folder)
        return 0

    strip = not args.no_strip and bool(junk)
    length = name_length_for(len(wallpapers), args.length)
    rng = random.Random(args.seed)
    rename_plan = [] if args.no_rename else build_rename_plan(wallpapers, length, rng, args.lowercase)
    # Subfolders are never moved, so a folder holding one cannot be rebuilt safely.
    compact = not args.no_compact and not subdirs

    # Slots after each step, so the plan shows where the saving actually comes from.
    slots_now = sum(slots_for(n) for n in files)
    renamed = dict(rename_plan)
    slots_final = sum(slots_for(renamed.get(n, n)) for n in files if not (strip and is_metadata(n)))

    print("Folder:      %s" % folder)
    print("Wallpapers:  %d" % len(wallpapers))
    print("macOS junk:  %d" % len(junk))
    if others:
        print("Other files: %d  (left alone)" % len(others))
    if subdirs:
        print("Subfolders:  %d  (compaction skipped — move them out to enable it)" % len(subdirs))
    print("Directory:   ~%d slots (~%d KB)  ->  ~%d slots (~%d KB)"
          % (slots_now, slots_now * 32 // 1024, slots_final, slots_final * 32 // 1024))

    print("\nPlan:")
    print("  1. strip    %s" % ("delete %d macOS metadata file(s)" % len(junk) if strip else "skipped"))
    print("  2. rename   %s" % ("%d file(s) to %s 8.3 names, base length %d"
                                % (len(rename_plan), "lowercase" if args.lowercase else "uppercase", length)
                                if rename_plan else "skipped"))
    print("  3. compact  %s" % ("rebuild %s in a fresh folder" % os.path.basename(folder)
                                if compact else "skipped"))

    if rename_plan:
        print("\nRename examples:")
        for old, new in rename_plan[:5]:
            print("  %s -> %s" % (old, new))
        if len(rename_plan) > 5:
            print("  ... and %d more" % (len(rename_plan) - 5))

    if not (strip or rename_plan or compact):
        print("\nNothing to do.")
        return 0
    if not args.apply:
        print("\nDry run. Nothing was changed. Re-run with --apply.")
        return 0

    failures = 0

    if strip:
        print("\n[1/3] Deleting %d macOS metadata file(s)..." % len(junk))
        removed = do_strip(folder, junk)
        print("      Deleted %d of %d." % (removed, len(junk)))
        failures += len(junk) - removed

    if rename_plan:
        print("\n[2/3] Renaming %d file(s)..." % len(rename_plan))
        done = do_rename(folder, rename_plan)
        print("      Renamed %d of %d." % (len(done), len(rename_plan)))
        failures += len(rename_plan) - len(done)
        if done:
            print("      Undo map: %s" % undo_map_path(folder))

    if compact:
        print("\n[3/3] Rebuilding the folder...")
        moved = do_compact(folder)
        print("      Moved %d file(s). %s now has a fresh directory." % (moved, folder))

    print("\nDone. Eject the card in Finder before removing it.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
