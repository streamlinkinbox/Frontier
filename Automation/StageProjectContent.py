#!/usr/bin/env python3
"""
Compile-time automatic EngineContent -> <Project>/Content staging tool.

Reads `[engine.content_imports].entries` from a `.projectspace` TOML file and copies
every declared directory or file from `EngineContent/<entry>` into `<ProjectRoot>/Content/<entry>`.
Writes `.staged_manifest.json` inside `<ProjectRoot>/Content/` and updates a CMake stamp file
so standalone projects and tests execute cleanly without `EngineContent/` present at runtime.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib  # type: ignore


def validate_relative_locator(locator: str) -> Path:
    if not locator or "\\" in locator or ":" in locator:
        raise ValueError(f"Invalid engine.content_imports locator: {locator!r}")
    rel = Path(locator)
    if rel.is_absolute() or ".." in rel.parts:
        raise ValueError(f"Confined relative path required in engine.content_imports: {locator!r}")
    return rel


def copy_if_changed(src_file: Path, dst_file: Path) -> bool:
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    if dst_file.exists():
        s_stat = src_file.stat()
        d_stat = dst_file.stat()
        if s_stat.st_size == d_stat.st_size and int(s_stat.st_mtime) == int(d_stat.st_mtime):
            return False
    shutil.copy2(src_file, dst_file)
    return True


def stage_imports(projectspace_path: Path, engine_content_dir: Path, project_content_dir: Path, stamp_path: Path) -> int:
    raw = projectspace_path.read_bytes()
    doc = tomllib.loads(raw.decode("utf-8"))

    engine_tbl = doc.get("engine", {})
    imports_tbl = engine_tbl.get("content_imports", {}) if isinstance(engine_tbl, dict) else {}
    entries = imports_tbl.get("entries", []) if isinstance(imports_tbl, dict) else []
    if not isinstance(entries, list):
        raise ValueError("[engine.content_imports].entries must be an array of relative strings")

    project_content_dir.mkdir(parents=True, exist_ok=True)

    staged_files: list[str] = []
    copied_count = 0
    missing_optional: list[str] = []

    for entry in entries:
        if not isinstance(entry, str):
            raise ValueError(f"Non-string entry in [engine.content_imports].entries: {entry!r}")
        rel = validate_relative_locator(entry)
        src = engine_content_dir / rel
        dst = project_content_dir / rel

        if not src.exists():
            # Fonts may be downloaded lazily via PrepareFonts.py; record cleanly if absent
            missing_optional.append(rel.as_posix())
            continue

        if src.is_file():
            if copy_if_changed(src, dst):
                copied_count += 1
            staged_files.append(rel.as_posix())
        elif src.is_dir():
            for root, _dirs, files in os.walk(src):
                root_path = Path(root)
                for fname in sorted(files):
                    if fname.endswith(".ppm"):
                        continue
                    s_file = root_path / fname
                    sub_rel = s_file.relative_to(engine_content_dir)
                    d_file = project_content_dir / sub_rel
                    if copy_if_changed(s_file, d_file):
                        copied_count += 1
                    staged_files.append(sub_rel.as_posix())

    manifest = {
        "projectspace": projectspace_path.name,
        "declared_entries": entries,
        "staged_file_count": len(staged_files),
        "copied_this_run": copied_count,
        "missing_sources": missing_optional,
        "staged_files": staged_files,
    }
    manifest_path = project_content_dir / ".staged_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    stamp_path.parent.mkdir(parents=True, exist_ok=True)
    stamp_path.write_text(
        f"staged={len(staged_files)} copied={copied_count}\n",
        encoding="utf-8",
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage declared EngineContent entries into <Project>/Content at build time.")
    parser.add_argument("--projectspace", required=True, type=Path)
    parser.add_argument("--engine-content", required=True, type=Path)
    parser.add_argument("--project-content", required=True, type=Path)
    parser.add_argument("--stamp", required=True, type=Path)
    args = parser.parse_args()
    return stage_imports(args.projectspace, args.engine_content, args.project_content, args.stamp)


if __name__ == "__main__":
    sys.exit(main())
