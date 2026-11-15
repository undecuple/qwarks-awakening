#!/usr/bin/env python3
"""Prepare work/ps3recomp-patched without modifying the pinned ps3recomp submodule.

This is the native Windows implementation and the implementation used by the
portable shell wrapper. It copies the submodule, applies port/patches/*.patch
with Git, synchronizes only changed files into the output tree, and writes the
patch-set stamp consumed by the lift and build guards.
"""
import argparse
import collections
import filecmp
import hashlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile


class PatchError(RuntimeError):
    pass


def _git(cwd, *args):
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as exc:
        raise PatchError(f"could not start Git: {exc}") from exc
    if result.returncode != 0:
        detail = result.stdout.strip() or f"Git exited with {result.returncode}"
        raise PatchError(detail)
    return result.stdout


def _patch_bytes(patch):
    return patch.read_bytes().replace(b"\r\n", b"\n")


def _apply_patch(root, scratch_rel, patch):
    try:
        result = subprocess.run(
            ["git", "apply", "--verbose", "--whitespace=nowarn", f"--directory={scratch_rel}", "-"],
            cwd=root,
            input=_patch_bytes(patch),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as exc:
        raise PatchError(f"could not start Git: {exc}") from exc
    output = result.stdout.decode("utf-8", errors="replace")
    if result.returncode != 0:
        raise PatchError(output.strip() or f"git apply exited with {result.returncode}")
    return output


def _patches(root):
    return sorted((root / "port" / "patches").glob("*.patch"))


def _duplicates(patch):
    names = []
    with patch.open(encoding="utf-8", errors="replace") as src:
        for line in src:
            if line.startswith("+++ b/"):
                names.append(line[6:].split("\t", 1)[0].rstrip("\r\n"))
    return sorted(name for name, count in collections.Counter(names).items() if count > 1)


def _patch_hash(patches):
    digest = hashlib.sha256()
    for patch in patches:
        digest.update(_patch_bytes(patch))
    return digest.hexdigest()[:16]


def _remove_owned(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def _export_commit(source, destination):
    try:
        result = subprocess.run(
            ["git", "archive", "--format=tar", "HEAD"],
            cwd=source,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise PatchError(f"could not start Git: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise PatchError(detail or f"git archive exited with {result.returncode}")

    destination.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:") as archive:
        for member in archive:
            rel = pathlib.PurePosixPath(member.name)
            if rel.is_absolute() or ".." in rel.parts:
                raise PatchError(f"the pinned toolkit archive has an unsafe path: {member.name}")
            target = destination.joinpath(*rel.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                source_file = archive.extractfile(member)
                if source_file is None:
                    raise PatchError(f"could not read {member.name} from the pinned toolkit archive")
                with source_file, target.open("wb") as output:
                    shutil.copyfileobj(source_file, output)
                target.chmod(member.mode)
            else:
                raise PatchError(f"the pinned toolkit archive has an unsupported entry: {member.name}")


def _same_file(left, right):
    return right.is_file() and filecmp.cmp(left, right, shallow=False)


def _sync_tree(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    changed = 0
    for directory, _, files in os.walk(source):
        rel = pathlib.Path(directory).relative_to(source)
        target_dir = destination / rel
        target_dir.mkdir(parents=True, exist_ok=True)
        for name in files:
            incoming = pathlib.Path(directory) / name
            target = target_dir / name
            if not _same_file(incoming, target):
                shutil.copyfile(incoming, target)
                shutil.copymode(incoming, target)
                changed += 1

    removed = 0
    for directory, dirs, files in os.walk(destination, topdown=False):
        rel = pathlib.Path(directory).relative_to(destination)
        source_dir = source / rel
        for name in files:
            if rel == pathlib.Path(".") and name == ".rc-patchset":
                continue
            if not (source_dir / name).is_file():
                (pathlib.Path(directory) / name).unlink()
                removed += 1
        for name in dirs:
            target = pathlib.Path(directory) / name
            if not (source_dir / name).is_dir() and target.is_dir():
                target.rmdir()
    return changed, removed


def prepare(root, output, say=print):
    root = pathlib.Path(root).resolve()
    source = root / "third_party" / "ps3recomp"
    if not (source / "CMakeLists.txt").is_file():
        raise PatchError("third_party/ps3recomp is missing: run git submodule update --init third_party/ps3recomp")

    destination = pathlib.Path(output)
    if not destination.is_absolute():
        destination = root / destination
    destination = destination.resolve()
    try:
        destination.relative_to(root)
    except ValueError as exc:
        raise PatchError("the patched output must be inside the repository") from exc
    if destination in (root, source) or destination == source.parent:
        raise PatchError("refusing an unsafe patched output path")

    patches = _patches(root)
    for patch in patches:
        duplicates = _duplicates(patch)
        if duplicates:
            names = ", ".join(duplicates)
            raise PatchError(f"patch {patch.relative_to(root)} names a file twice: {names}")

    scratch = pathlib.Path(str(destination) + ".new")
    _remove_owned(scratch)
    scratch.parent.mkdir(parents=True, exist_ok=True)
    try:
        _export_commit(source, scratch)
        scratch_rel = scratch.relative_to(root).as_posix()
        for patch in patches:
            patch_rel = patch.relative_to(root).as_posix()
            output = _apply_patch(root, scratch_rel, patch)
            if not any(line.startswith("Applied patch ") and line.endswith(" cleanly.") for line in output.splitlines()):
                raise PatchError(f"patch {patch_rel} did not report a clean apply: {output.strip()}")
            say(f"applied {patch_rel}")

        changed, removed = _sync_tree(scratch, destination)
    finally:
        _remove_owned(scratch)

    commit = _git(source, "rev-parse", "--short", "HEAD").strip()
    stamp = f"{_patch_hash(patches)}-{commit}"
    (destination / ".rc-patchset").write_text(stamp + "\n", encoding="utf-8", newline="\n")
    say(f"synced: {changed} file(s) changed, {removed} removed")
    return stamp


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--out", default=os.environ.get("RC_PATCHED_DIR", "work/ps3recomp-patched"))
    args = parser.parse_args(argv)
    try:
        prepare(args.root, args.out)
    except (OSError, PatchError) as exc:
        print(f"ps3recomp-patched: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
