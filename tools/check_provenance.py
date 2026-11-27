#!/usr/bin/env python3
"""Check every file Fractal copied from Temporal-5D against PROVENANCE.md.

For each row of the "Copied files" table:
- a "record only" file must hash to its source SHA-256, unless the file is listed
  under "Changes since the copy", whose table gives the SHA-256 the whole file has
  now (something pins these files' bytes, so they carry no header to say so);
- a "header" file must start with its provenance header (after a shebang, where
  there is one), and the bytes below the header must hash to its source SHA-256,
  unless the file is listed under "Changes since the copy", whose table gives the
  SHA-256 the bytes below the header have now.

Every other tracked file in a directory that holds a copied file, or below one, must
be one the "Fractal's own files" section lists, by name or by a pattern ending in
"/**": a file dropped into the copied trees without a row is reported, so nothing
gets in under cover of the copy (NF11, NF12).

It needs nothing but the repository: no clone of Temporal-5D (NS2).
Usage: python tools/check_provenance.py [--root DIR]
       python tools/check_provenance.py --hash PATH...   the SHA-256 of each file's
           bytes below its header (of the whole file, for a record-only one), for
           a "Changes since the copy" row
Exit status 0 when every file checks, 1 otherwise.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

PIN = "c8a9c490bfde650466609531f8083d69513c6036"
ROW = re.compile(r"^\| `([^`]+)` \| (header|record only) \| (\d+) \| `([0-9a-f]{64})` \|")
CHANGED_ROW = re.compile(r"^\| `([^`]+)` \| [^|]+ \| `([0-9a-f]{64})` \|")
OWN_ROW = re.compile(r"^\| `([^`]+)` \|")


def sections(text: str) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    current = None
    for line in text.splitlines():
        if line.startswith("## "):
            current = line[3:].strip()
            out[current] = []
        elif current is not None:
            out[current].append(line)
    return out


def body_below_header(path: str, data: bytes) -> tuple[bytes | None, str]:
    """The bytes below the provenance header, or None and why not."""
    lines = data.split(b"\n")
    if path.endswith(".md"):
        if len(lines) < 8 or lines[0] != b"<!--" or lines[6] != b"-->":
            return None, "no Markdown provenance header"
        header, body = lines[1:6], lines[7:]
        keep: list[bytes] = []
        prefix = b""
    else:
        k = 1 if lines and lines[0].startswith(b"#!") else 0
        if len(lines) < k + 5:
            return None, "too short for a provenance header"
        header, body, keep = lines[k:k + 5], lines[k + 5:], lines[:k]
        prefix = b"// " if path.endswith((".cpp", ".hpp", ".h")) else b"# "
        if not all(h.startswith(prefix) for h in header):
            return None, "no provenance header"
        header = [h[len(prefix):] for h in header]
    expect = [b"Fractal provenance: copied from Temporal-5D",
              b"  source: " + path.encode(), b"  commit: " + PIN.encode(), b"  copied: ", b"  state:  "]
    for got, want in zip(header, expect):
        if not got.startswith(want):
            return None, f"header line {got!r} does not start {want!r}"
    return b"\n".join(keep + body), ""


def tracked_files(root: Path) -> list[str] | None:
    """Every file git tracks, and every untracked one it does not ignore (gitlinks left
    out); None outside a git work tree."""
    def git(*args: str) -> list[str]:
        out = subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=True)
        return [e for e in out.stdout.decode("utf-8", errors="replace").split("\0") if e]

    try:
        staged = git("ls-files", "-s", "-z")
        others = git("ls-files", "--others", "--exclude-standard", "-z")
    except (OSError, subprocess.CalledProcessError):
        return None
    paths = []
    for entry in staged:
        meta, _, path = entry.partition("\t")
        if not meta.startswith("160000"):  # a submodule's gitlink is the harness's to check
            paths.append(path)
    return paths + [path for path in others if not path.endswith("/")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--hash", nargs="+", metavar="PATH",
                    help="print the SHA-256 of each file's bytes below its provenance header, and stop")
    a = ap.parse_args()
    root = Path(a.root)
    sec = sections((root / "PROVENANCE.md").read_text(encoding="utf-8"))
    rows = [m.groups() for line in sec.get("Copied files", []) if (m := ROW.match(line))]
    if a.hash:
        record_only = {path for path, mode, _, _ in rows if mode == "record only"}
        status = 0
        for path in a.hash:
            rel = Path(path).as_posix()
            data = (root / rel).read_bytes()
            body, why = (data, "") if rel in record_only else body_below_header(rel, data)
            if body is None:
                print(f"FAIL     {rel}: {why}")
                status = 1
            else:
                print(f"{hashlib.sha256(body).hexdigest()}  {rel}")
        return status
    changed = {m.group(1): m.group(2) for line in sec.get("Changes since the copy", [])
               if (m := CHANGED_ROW.match(line))}
    failures = 0
    n_unchanged = n_changed = 0
    for path, mode, size, digest in rows:
        f = root / path
        if not f.is_file():
            print(f"MISSING  {path}")
            failures += 1
            continue
        data = f.read_bytes()
        if mode == "record only" and path in changed:
            ok = hashlib.sha256(data).hexdigest() == changed[path]
            why = "bytes differ from the recorded change"
        elif mode == "record only":
            ok = len(data) == int(size) and hashlib.sha256(data).hexdigest() == digest
            why = "bytes differ from the source"
        else:
            body, why = body_below_header(path, data)
            want = changed.get(path, digest)
            ok = body is not None and hashlib.sha256(body).hexdigest() == want
            if body is not None and not ok:
                why = "bytes below the header differ from " + ("the recorded change" if path in changed else "the source")
        if not ok:
            print(f"FAIL     {path}: {why}")
            failures += 1
        elif path in changed:
            n_changed += 1
        else:
            n_unchanged += 1
    copied = {r[0] for r in rows}
    for path in changed:
        if path not in copied:
            print(f"FAIL     {path}: listed as changed but not in the table of copied files")
            failures += 1
    # A changed file's header must say so, and an unchanged one must not.
    for path, mode, _, _ in rows:
        f = root / path
        if mode != "header" or not f.is_file():
            continue
        says_changed = b"state:  changed" in f.read_bytes()[:1200]
        if says_changed != (path in changed):
            print(f"FAIL     {path}: its header says {'changed' if says_changed else 'unchanged'}, "
                  f"PROVENANCE.md says {'changed' if path in changed else 'unchanged'}")
            failures += 1
    # Nothing in the copied trees without a row: copied, or Fractal's own. The copied
    # trees are the directories that hold a copied file, with everything below them
    # (read from the table, so this file names none of them).
    copied_trees = tuple(sorted({path.rsplit("/", 1)[0] + "/" for path in copied if "/" in path}))
    own = [m.group(1) for line in sec.get("Fractal's own files", []) if (m := OWN_ROW.match(line))]
    own_exact = {p for p in own if not p.endswith("/**")}
    own_prefix = tuple(p[:-2] for p in own if p.endswith("/**"))
    tracked = tracked_files(root)
    n_own = 0
    if tracked is None:
        print("NOTE     not a git work tree: files without a row are not looked for")
    else:
        for path in tracked:
            if not path.startswith(copied_trees) or path in copied:
                continue
            if path in own_exact or (own_prefix and path.startswith(own_prefix)):
                n_own += 1
                continue
            print(f"FAIL     {path}: in a copied tree with no row - copied files go in \"Copied files\", "
                  "Fractal's own in \"Fractal's own files\"")
            failures += 1
        for path in sorted(own_exact):
            if not (root / path).is_file():
                print(f"FAIL     {path}: listed as Fractal's own but not in the tree")
                failures += 1
    print(f"provenance: {len(rows)} copied files; {n_unchanged} unchanged, {n_changed} changed as recorded, "
          f"{n_own} of Fractal's own in the copied trees, {failures} failing")
    return 1 if failures or not rows else 0


if __name__ == "__main__":
    sys.exit(main())
