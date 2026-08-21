#!/usr/bin/env python3

"""Developer gateway for configuring and building icecast-cxx.

CMake remains the build-system source of truth. This script prepares pinned
repository-local dependency sources, then invokes CMake with explicit paths so
those sources are reused instead of fetched a second time.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
MANIFEST_PATH = ROOT / "dependencies.json"
ARCHIVE_MARKER = ".icecast-cxx-dependency.json"


class build_error(RuntimeError):
    pass


def run(command: list[str], *, cwd: Path | None = None, capture: bool = False) -> str:
    print("+", " ".join(command))
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if result.returncode != 0:
        detail = ""
        if capture:
            detail = (result.stderr or result.stdout or "").strip()
        if detail:
            raise build_error(f"command failed ({result.returncode}): {detail}")
        raise build_error(f"command failed with exit code {result.returncode}")
    return (result.stdout or "").strip() if capture else ""


def load_dependency_manifest() -> list[dict[str, Any]]:
    try:
        data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise build_error(f"missing dependency manifest: {MANIFEST_PATH}") from error
    except json.JSONDecodeError as error:
        raise build_error(f"invalid dependencies.json: {error}") from error

    if data.get("schema_version") != 1:
        raise build_error("unsupported dependencies.json schema_version")

    dependencies = data.get("dependencies")
    if not isinstance(dependencies, list):
        raise build_error("dependencies.json must contain a dependencies array")

    return dependencies


def require_manifest_string(dependency: dict[str, Any], key: str) -> str:
    value = dependency.get(key)
    if not isinstance(value, str) or not value:
        name = dependency.get("name", "<unnamed>")
        raise build_error(f"dependency {name!r} is missing required string field {key!r}")
    return value


def dependency_applies_to_platform(dependency: dict[str, Any], platform: str) -> bool:
    platforms = dependency.get("platforms")
    if platforms is None:
        return True
    if not isinstance(platforms, list) or not platforms:
        name = dependency.get("name", "<unnamed>")
        raise build_error(f"dependency {name!r} has an invalid platforms field")
    for value in platforms:
        if not isinstance(value, str) or not value:
            name = dependency.get("name", "<unnamed>")
            raise build_error(f"dependency {name!r} has an invalid platforms field")
    return platform in platforms


def dependency_source_result(dependency: dict[str, Any], destination: Path) -> tuple[str, Path] | None:
    source_variable = dependency.get("cmake_source_variable")
    if source_variable is None:
        return None
    if not isinstance(source_variable, str) or not source_variable:
        name = dependency.get("name", "<unnamed>")
        raise build_error(f"dependency {name!r} has an invalid cmake_source_variable")
    return source_variable, destination


def git_head(directory: Path) -> str:
    return run(["git", "-C", str(directory), "rev-parse", "HEAD"], capture=True)


def git_is_dirty(directory: Path) -> bool:
    return bool(run(["git", "-C", str(directory), "status", "--porcelain"], capture=True))


def checkout_git_dependency(dependency: dict[str, Any], dependencies_dir: Path, *, offline: bool) -> tuple[str, Path] | None:
    name = require_manifest_string(dependency, "name")
    repository = require_manifest_string(dependency, "repository")
    revision = require_manifest_string(dependency, "revision")
    directory_name = dependency.get("directory", name)

    if not isinstance(directory_name, str) or not directory_name:
        raise build_error(f"dependency {name!r} has an invalid directory field")

    destination = dependencies_dir / directory_name

    if not destination.exists():
        if offline:
            raise build_error(f"dependency {name!r} is missing at {destination} while --offline is active")
        run(["git", "clone", "--no-checkout", repository, str(destination)])
        run(["git", "-C", str(destination), "fetch", "--depth", "1", "origin", revision])
        run(["git", "-C", str(destination), "checkout", "--detach", "FETCH_HEAD"])
    else:
        if not (destination / ".git").exists():
            raise build_error(f"dependency path exists but is not a Git checkout: {destination}")

        current_revision = git_head(destination)
        if current_revision != revision:
            if git_is_dirty(destination):
                raise build_error(
                    f"dependency {name!r} is locally modified at {destination}; "
                    "refusing to change its pinned revision"
                )
            if offline:
                raise build_error(
                    f"dependency {name!r} is at {current_revision}, expected {revision}, "
                    "and --offline prevents updating it"
                )
            run(["git", "-C", str(destination), "fetch", "--depth", "1", "origin", revision])
            run(["git", "-C", str(destination), "checkout", "--detach", "FETCH_HEAD"])

    actual_revision = git_head(destination)
    if actual_revision != revision:
        raise build_error(
            f"dependency {name!r} resolved to {actual_revision}, expected exact revision {revision}"
        )

    return dependency_source_result(dependency, destination)


def archive_marker_data(dependency: dict[str, Any]) -> dict[str, str]:
    return {
        "name": require_manifest_string(dependency, "name"),
        "url": require_manifest_string(dependency, "url"),
        "sha256": require_manifest_string(dependency, "sha256").lower(),
    }


def archive_destination_matches(dependency: dict[str, Any], destination: Path) -> bool:
    marker_path = destination / ARCHIVE_MARKER
    if not marker_path.is_file():
        return False
    try:
        marker = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return marker == archive_marker_data(dependency)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def download_file(url: str, destination: Path) -> None:
    print(f"+ download {url}")
    try:
        with urllib.request.urlopen(url) as response, destination.open("wb") as output:
            shutil.copyfileobj(response, output)
    except OSError as error:
        raise build_error(f"failed to download {url}: {error}") from error


def safe_extract_tar(archive: Path, destination: Path) -> Path:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()

    try:
        with tarfile.open(archive, "r:*") as tar:
            members = tar.getmembers()
            top_levels: set[str] = set()
            for member in members:
                member_path = Path(member.name)
                if member_path.is_absolute() or (".." in member_path.parts):
                    raise build_error(f"archive contains unsafe path: {member.name}")
                if member.issym() or member.islnk():
                    raise build_error(f"archive contains unsupported link entry: {member.name}")
                if member_path.parts:
                    top_levels.add(member_path.parts[0])
                target = (destination / member_path).resolve()
                if (target != root) and (root not in target.parents):
                    raise build_error(f"archive contains unsafe path: {member.name}")
            tar.extractall(destination)
    except (tarfile.TarError, OSError) as error:
        raise build_error(f"failed to extract archive {archive}: {error}") from error

    if len(top_levels) != 1:
        raise build_error("dependency archive must contain exactly one top-level directory")
    extracted_root = destination / next(iter(top_levels))
    if not extracted_root.is_dir():
        raise build_error("dependency archive top-level entry is not a directory")
    return extracted_root


def checkout_archive_dependency(dependency: dict[str, Any], dependencies_dir: Path, *, offline: bool) -> tuple[str, Path] | None:
    name = require_manifest_string(dependency, "name")
    url = require_manifest_string(dependency, "url")
    expected_sha256 = require_manifest_string(dependency, "sha256").lower()
    if len(expected_sha256) != 64 or any(character not in "0123456789abcdef" for character in expected_sha256):
        raise build_error(f"dependency {name!r} has an invalid sha256 field")

    directory_name = dependency.get("directory", name)
    if not isinstance(directory_name, str) or not directory_name:
        raise build_error(f"dependency {name!r} has an invalid directory field")
    destination = dependencies_dir / directory_name

    if destination.exists():
        if archive_destination_matches(dependency, destination):
            return dependency_source_result(dependency, destination)
        if not (destination / ARCHIVE_MARKER).exists():
            print(f"Using developer-provided dependency source at {destination}")
            return dependency_source_result(dependency, destination)
        raise build_error(
            f"dependency {name!r} at {destination} was prepared from a different archive; "
            "refusing to replace it automatically. Remove that directory explicitly to refresh it."
        )

    if offline:
        raise build_error(f"dependency {name!r} is missing at {destination} while --offline is active")

    dependencies_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f"icecast-cxx-{name}-", dir=dependencies_dir) as temporary:
        temporary_path = Path(temporary)
        archive_path = temporary_path / "source.archive"
        extract_path = temporary_path / "extract"
        download_file(url, archive_path)
        actual_sha256 = sha256_file(archive_path)
        if actual_sha256 != expected_sha256:
            raise build_error(
                f"dependency {name!r} archive checksum mismatch: got {actual_sha256}, expected {expected_sha256}"
            )
        extracted_root = safe_extract_tar(archive_path, extract_path)
        shutil.move(str(extracted_root), str(destination))

    (destination / ARCHIVE_MARKER).write_text(
        json.dumps(archive_marker_data(dependency), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return dependency_source_result(dependency, destination)


def prepare_dependencies(dependencies_dir: Path, *, offline: bool, platform: str) -> list[tuple[str, Path]]:
    dependencies = load_dependency_manifest()
    dependencies_dir.mkdir(parents=True, exist_ok=True)

    cmake_sources: list[tuple[str, Path]] = []
    for dependency in dependencies:
        if not isinstance(dependency, dict):
            raise build_error("each dependencies.json entry must be an object")
        if not dependency_applies_to_platform(dependency, platform):
            continue

        dependency_type = dependency.get("type", "git")
        if dependency_type == "git":
            source = checkout_git_dependency(dependency, dependencies_dir, offline=offline)
        elif dependency_type == "archive":
            source = checkout_archive_dependency(dependency, dependencies_dir, offline=offline)
        else:
            name = dependency.get("name", "<unnamed>")
            raise build_error(f"unsupported dependency type {dependency_type!r} for {name!r}")
        if source is not None:
            cmake_sources.append(source)

    return cmake_sources


def cmake_bool(value: bool) -> str:
    return "ON" if value else "OFF"


def default_build_dir(platform: str) -> Path:
    return ROOT / "build" / platform


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare dependencies, configure CMake, build icecast-cxx, and run tests."
    )
    parser.add_argument("--platform", choices=("native", "web"), default="native")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--dependencies-dir", type=Path, default=ROOT / "dependencies")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--generator")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--clean", action="store_true", help="remove the selected build directory before configuring")
    parser.add_argument("--offline", action="store_true", help="disable all dependency downloads, including CMake dependency fallbacks")
    parser.add_argument("--no-tests", action="store_true")
    parser.add_argument("--configure-only", action="store_true")
    return parser.parse_args()


def configure(args: argparse.Namespace, build_dir: Path, dependencies_dir: Path, cmake_sources: list[tuple[str, Path]]) -> None:
    command: list[str] = []
    if args.platform == "web":
        emcmake = shutil.which("emcmake")
        if emcmake is None:
            raise build_error("--platform web requires Emscripten's emcmake on PATH")
        command.append(emcmake)

    command.extend(
        [
            args.cmake,
            "-S",
            str(ROOT),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={args.configuration}",
            f"-DICECAST_CXX_DEPENDENCIES_DIR={dependencies_dir}",
            f"-DICECAST_CXX_FETCH_DEPENDENCIES={cmake_bool(not args.offline)}",
            f"-DICECAST_CXX_BUILD_TESTS={cmake_bool(not args.no_tests)}",
            "-DICECAST_CXX_INSTALL=ON",
            "-DICECAST_CXX_ENABLE_STREAM=ON",
            "-DICECAST_CXX_ENABLE_ADMIN=ON",
        ]
    )

    if args.platform == "native":
        command.extend(
            [
                "-DICECAST_CXX_ENABLE_TRANSPORT_CURL=ON",
                "-DICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT=ON",
                "-DICECAST_CXX_ENABLE_TRANSPORT_WEB=OFF",
            ]
        )
    else:
        command.extend(
            [
                "-DICECAST_CXX_ENABLE_TRANSPORT_CURL=OFF",
                "-DICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT=OFF",
                "-DICECAST_CXX_ENABLE_TRANSPORT_WEB=ON",
            ]
        )

    if args.generator:
        command.extend(["-G", args.generator])

    for variable, source_path in cmake_sources:
        command.append(f"-D{variable}={source_path}")

    command.extend(args.cmake_arg)
    run(command)


def build(args: argparse.Namespace, build_dir: Path) -> None:
    command = [args.cmake, "--build", str(build_dir), "--config", args.configuration]
    if args.jobs is not None:
        if args.jobs < 1:
            raise build_error("--jobs must be greater than zero")
        command.extend(["--parallel", str(args.jobs)])
    for target in args.target:
        command.extend(["--target", target])
    run(command)


def test(args: argparse.Namespace, build_dir: Path) -> None:
    run(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--build-config",
            args.configuration,
            "--output-on-failure",
        ]
    )


def main() -> int:
    args = parse_arguments()
    build_dir = (args.build_dir or default_build_dir(args.platform)).resolve()
    dependencies_dir = args.dependencies_dir.resolve()

    try:
        if args.clean and build_dir.exists():
            print(f"Removing {build_dir}")
            shutil.rmtree(build_dir)

        cmake_sources = prepare_dependencies(dependencies_dir, offline=args.offline, platform=args.platform)
        configure(args, build_dir, dependencies_dir, cmake_sources)

        if args.configure_only:
            return 0

        build(args, build_dir)
        if not args.no_tests:
            test(args, build_dir)
        return 0
    except build_error as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
