#!/usr/bin/env python3

"""Developer gateway for configuring and building icecast-cxx.

CMake remains the build-system source of truth. This script prepares pinned
repository-local dependency sources, then invokes CMake with explicit paths so
those sources are reused instead of fetched a second time.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
MANIFEST_PATH = ROOT / "dependencies.json"


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
        raise build_error(f"invalid dependency manifest: {error}") from error

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

    source_variable = dependency.get("cmake_source_variable")
    if source_variable is None:
        return None
    if not isinstance(source_variable, str) or not source_variable:
        raise build_error(f"dependency {name!r} has an invalid cmake_source_variable")

    return source_variable, destination


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
        if dependency_type != "git":
            name = dependency.get("name", "<unnamed>")
            raise build_error(f"unsupported dependency type {dependency_type!r} for {name!r}")

        source = checkout_git_dependency(dependency, dependencies_dir, offline=offline)
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
    parser.add_argument("--offline", action="store_true", help="disable all dependency downloads, including CMake FetchContent fallback")
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
