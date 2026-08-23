#!/usr/bin/env python3
"""Publish immutable firmware rebuild records to a durable GitHub branch.

The branch contains small JSON provenance records only. Existing paths are
never overwritten: an identical record is treated as an idempotent retry and a
different record at the same path fails closed.
"""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
from typing import Sequence
from urllib.parse import quote

from firmware_build_metadata import metadata_filename, normalize_repository
from reconstruct_firmware_elf import normalize_commit, request_from_record


DEFAULT_BRANCH = "firmware-build-metadata"
MAX_PUBLISH_ATTEMPTS = 5
MAX_RECORD_BYTES = 1024 * 1024


class PublishError(RuntimeError):
    pass


def run_gh(arguments: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["gh", *arguments],
        capture_output=True,
        text=True,
        check=False,
    )


def gh_json(arguments: Sequence[str]) -> object:
    completed = run_gh(arguments)
    if completed.returncode != 0:
        raise PublishError(completed.stderr.strip() or "GitHub API request failed")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as err:
        raise PublishError("GitHub API returned invalid JSON") from err


def ensure_branch(repository: str, branch: str, start_sha: str) -> None:
    endpoint = f"repos/{repository}/git/ref/heads/{branch}"
    if run_gh(["api", endpoint]).returncode == 0:
        return
    created = run_gh(
        [
            "api",
            "--method",
            "POST",
            f"repos/{repository}/git/refs",
            "-f",
            f"ref=refs/heads/{branch}",
            "-f",
            f"sha={start_sha}",
        ]
    )
    if created.returncode != 0 and run_gh(["api", endpoint]).returncode != 0:
        raise PublishError(created.stderr.strip() or f"Cannot create branch {branch}")


def load_record(path: Path) -> tuple[dict[str, object], str]:
    try:
        size = path.stat().st_size
    except OSError as err:
        raise PublishError(f"Cannot inspect build metadata {path}: {err}") from err
    if size > MAX_RECORD_BYTES:
        raise PublishError(
            f"Build metadata exceeds the {MAX_RECORD_BYTES}-byte durable-record limit: {path}"
        )
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise PublishError(f"Cannot read build metadata {path}: {err}") from err
    if not isinstance(record, dict):
        raise PublishError(f"Build metadata must be a JSON object: {path}")
    request = request_from_record(record)
    expected_name = metadata_filename(record)
    if path.name != expected_name:
        raise PublishError(
            f"Build metadata filename is not build-addressed ({path.name} != {expected_name})"
        )
    owner, repository = request.source_repository.split("/", 1)
    destination = PurePosixPath(
        "records",
        owner,
        repository,
        request.source_commit,
        request.expected_build_id,
        expected_name,
    )
    return record, str(destination)


def fetch_existing(repository: str, branch: str, destination: str) -> bytes | None:
    endpoint = f"repos/{repository}/contents/{destination}"
    completed = run_gh(["api", "--method", "GET", endpoint, "-f", f"ref={branch}"])
    if completed.returncode != 0:
        if "HTTP 404" in completed.stderr:
            return None
        raise PublishError(completed.stderr.strip() or f"Cannot read {destination}")
    try:
        response = json.loads(completed.stdout)
        encoded = response["content"]
        encoding = response["encoding"]
        if encoding != "base64" or not isinstance(encoded, str):
            raise KeyError
        return base64.b64decode(encoded.replace("\n", ""), validate=True)
    except (json.JSONDecodeError, KeyError, ValueError) as err:
        raise PublishError(f"GitHub returned invalid content for {destination}") from err


def same_rebuild_contract(existing: bytes, candidate: bytes) -> bool:
    try:
        existing_record = json.loads(existing)
        candidate_record = json.loads(candidate)
        if not isinstance(existing_record, dict) or not isinstance(candidate_record, dict):
            return False
        return request_from_record(existing_record) == request_from_record(candidate_record)
    except (json.JSONDecodeError, RuntimeError, UnicodeDecodeError):
        return False


def publish_record(
    *,
    repository: str,
    branch: str,
    path: Path,
) -> str:
    _, destination = load_record(path)
    content = path.read_bytes()
    if len(content) > MAX_RECORD_BYTES:
        raise PublishError(
            f"Build metadata exceeds the {MAX_RECORD_BYTES}-byte durable-record limit: {path}"
        )
    encoded = base64.b64encode(content).decode("ascii")
    endpoint = f"repos/{repository}/contents/{destination}"

    for attempt in range(MAX_PUBLISH_ATTEMPTS):
        existing = fetch_existing(repository, branch, destination)
        if existing is not None:
            if existing != content and not same_rebuild_contract(existing, content):
                raise PublishError(
                    "Immutable build metadata path already has a different rebuild contract: "
                    f"{destination}"
                )
            break
        completed = run_gh(
            [
                "api",
                "--method",
                "PUT",
                endpoint,
                "-f",
                f"message=Publish firmware build metadata {path.name}",
                "-f",
                f"content={encoded}",
                "-f",
                f"branch={branch}",
            ]
        )
        if completed.returncode == 0:
            break
        if attempt == MAX_PUBLISH_ATTEMPTS - 1:
            raise PublishError(completed.stderr.strip() or f"Cannot publish {destination}")
        time.sleep(2**attempt)
    else:  # pragma: no cover - loop exits through break or raises
        raise PublishError(f"Cannot publish {destination}")

    encoded_path = "/".join(quote(part, safe="") for part in destination.split("/"))
    return f"https://raw.githubusercontent.com/{repository}/{branch}/{encoded_path}"


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--github-repository", required=True)
    parser.add_argument("--branch", default=DEFAULT_BRANCH)
    parser.add_argument("--start-sha", required=True)
    parser.add_argument("metadata", type=Path, nargs="+")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    try:
        repository = normalize_repository(args.github_repository)
        start_sha = normalize_commit(args.start_sha)
        if args.branch != DEFAULT_BRANCH:
            raise PublishError(f"Only the durable {DEFAULT_BRANCH!r} branch is supported")
        ensure_branch(repository, args.branch, start_sha)
        urls = [
            publish_record(
                repository=repository,
                branch=args.branch,
                path=path.resolve(),
            )
            for path in args.metadata
        ]
    except (PublishError, RuntimeError) as err:
        raise SystemExit(str(err)) from err
    print(json.dumps({"metadata_urls": urls}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
