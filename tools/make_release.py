#!/usr/bin/env python3
"""Build a self-hosted PS5 Harness release bundle.

The bundle is intentionally explicit: it never copies the worktree, `.deps`,
build outputs, configs, logs, credentials or local certificates. It copies the
validated title package and the documentation/helpers a user needs to run the
harness on their own LAN.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "dist"
PACKAGE_REL = Path(".deps") / "ps5-build"


def fail(message: str) -> int:
    print(f"release: {message}", file=sys.stderr)
    return 1


def read_title_id() -> str:
    text = (ROOT / "platform/ps5/title.mk").read_text(encoding="utf-8")
    match = re.search(r"^TITLE_ID\s*:=\s*(\S+)", text, re.MULTILINE)
    if not match:
        raise ValueError("TITLE_ID not found in platform/ps5/title.mk")
    return match.group(1)


def read_version() -> str:
    value = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not value:
        raise ValueError("VERSION is empty")
    return value


def read_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "--short=12", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "nogit"


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise FileNotFoundError(source)
    shutil.copytree(source, destination)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_bundle(package: Path, output_root: Path) -> tuple[Path, Path]:
    title_id = read_title_id()
    version = read_version()
    commit = read_commit()
    bundle_name = f"ps5-harness-{version}-{commit}"
    staging = output_root / bundle_name

    required = [
        package / "eboot.bin",
        package / "harness.elf",
        package / "sce_sys" / "param.sfo",
        package / "sce_sys" / "icon0.png",
    ]
    missing = [str(item.relative_to(ROOT)) for item in required if not item.is_file()]
    if missing:
        raise FileNotFoundError(
            "missing package artifacts: " + ", ".join(missing)
        )

    if output_root.exists():
        output_root.mkdir(parents=True, exist_ok=True)
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    copy_tree(package, staging / title_id)
    copy_file(ROOT / "docs/RELEASE.md", staging / "START-HERE.md")
    copy_file(ROOT / "README.md", staging / "README.md")
    copy_file(ROOT / "LICENSE", staging / "LICENSE")
    copy_file(ROOT / "config.example.json", staging / "config.example.json")
    copy_file(ROOT / "config.console.json", staging / "config.console.json")
    copy_file(ROOT / "THIRD-PARTY-NOTICES.md", staging / "THIRD-PARTY-NOTICES.md")
    copy_tree(ROOT / "licenses", staging / "licenses")
    copy_tree(ROOT / "examples", staging / "examples")
    # Keep internal console evidence and planning notes out of a user-facing
    # bundle. Only the support-facing docs are shipped.
    for name in ("SECURITY.md", "RECOVERY.md", "HARDWARE-CHECKLIST.md"):
        copy_file(ROOT / "docs" / name, staging / "docs" / name)
    copy_file(ROOT / "tools/ps5h-discover.py", staging / "tools/ps5h-discover.py")

    build_info = (
        "PS5 Harness self-hosted release\n"
        f"version={version}\n"
        f"commit={commit}\n"
        f"title_id={title_id}\n"
        "server=local-only\n"
        "authors_server=none\n"
        f"built_utc={datetime.now(timezone.utc).isoformat()}\n"
    )
    (staging / "BUILD-INFO.txt").write_text(build_info, encoding="utf-8")

    checksum_lines = []
    for path in sorted(staging.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            checksum_lines.append(
                f"{sha256_file(path)}  {path.relative_to(staging).as_posix()}"
            )
    (staging / "SHA256SUMS").write_text(
        "\n".join(checksum_lines) + "\n", encoding="utf-8"
    )

    zip_path = output_root / f"{bundle_name}.zip"
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(staging).as_posix())

    zip_hash = sha256_file(zip_path)
    (output_root / f"{bundle_name}.zip.sha256").write_text(
        f"{zip_hash}  {zip_path.name}\n", encoding="utf-8"
    )
    return zip_path, output_root / f"{bundle_name}.zip.sha256"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package",
        type=Path,
        default=None,
        help="validated title package directory (default: .deps/ps5-build/<TITLE_ID>)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help="release output directory (default: dist)",
    )
    args = parser.parse_args()

    package = args.package
    if package is None:
        try:
            package = ROOT / PACKAGE_REL / read_title_id()
        except ValueError as error:
            return fail(str(error))
    if not package.is_dir():
        return fail(
            f"package directory not found: {package}; run "
            "`make -C platform/ps5 package` first"
        )

    try:
        zip_path, sha_path = build_bundle(package, args.out)
    except (OSError, ValueError) as error:
        return fail(str(error))

    print(f"release: {zip_path}")
    print(f"release: {sha_path}")
    print(f"release: {zip_path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
