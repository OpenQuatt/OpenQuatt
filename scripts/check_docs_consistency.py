#!/usr/bin/env python3
"""Check exact documentation contracts and changed-file documentation impact.

Exact contract violations always fail. Changed-file impact findings are warnings
by default (advisory, see #518) and fail when ``--strict`` is used for pull requests.
PR-template documentatiekeuzes zijn exclusief: exact één checkbox moet geselecteerd zijn.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DOCS_IMPACT_PATH = REPO_ROOT / ".github/docs-impact.json"
NO_DOCS_CHECKBOX = "Geen documentatiewijziging nodig"
DOCS_UPDATED_CHECKBOX = "Documentatie bijgewerkt voor de gebruikersgerichte wijziging"
DOCS_MOTIVATION_LABEL = "Docs-impact motivatie:"


@dataclass
class Finding:
    file: str
    line: int
    message: str
    severity: str = "error"


def run_git(args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git command failed")
    return result.stdout


def changed_files_for_ci() -> set[str]:
    event = os.getenv("GITHUB_EVENT_NAME", "")
    if event == "pull_request":
        base_ref = os.getenv("GITHUB_BASE_REF", "").strip()
        if not base_ref:
            return set()
        # Best effort fetch of base branch for a stable diff range.
        subprocess.run(
            ["git", "fetch", "--no-tags", "origin", base_ref],
            cwd=REPO_ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            text=True,
        )
        try:
            diff = run_git(["diff", "--name-only", f"origin/{base_ref}...HEAD"])
            return {line.strip() for line in diff.splitlines() if line.strip()}
        except RuntimeError:
            # Shallow checkout of merge commit may have no merge base; fallback to empty set
            # so PR-metadata (checkbox) checks still run.
            return set()

    if event == "push":
        try:
            diff = run_git(["diff", "--name-only", "HEAD~1...HEAD"])
            return {line.strip() for line in diff.splitlines() if line.strip()}
        except RuntimeError:
            return set()

    return set()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def lines_with_phrase(path: Path, phrase: str) -> Iterable[int]:
    needle = phrase.lower()
    for idx, line in enumerate(read_text(path).splitlines(), start=1):
        if needle in line.lower():
            yield idx


def add(
    findings: list[Finding],
    file: str,
    line: int,
    message: str,
    severity: str = "error",
) -> None:
    findings.append(Finding(file=file, line=line, message=message, severity=severity))


def any_changed(changed: set[str], candidates: set[str]) -> bool:
    return bool(changed.intersection(candidates))


def load_docs_impact_rules() -> list[dict[str, object]]:
    try:
        data = json.loads(read_text(DOCS_IMPACT_PATH))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot read documentation impact map: {exc}") from exc

    if not isinstance(data, dict) or data.get("version") != 1:
        raise ValueError("Documentation impact map must be an object with version 1.")
    rules = data.get("rules")
    if not isinstance(rules, list) or not rules:
        raise ValueError("Documentation impact map must contain a non-empty rules list.")

    for index, rule in enumerate(rules, start=1):
        if not isinstance(rule, dict):
            raise ValueError(f"Documentation impact rule {index} must be an object.")
        name = rule.get("name")
        sources = rule.get("source_patterns")
        docs = rule.get("docs_any_of")
        if not isinstance(name, str) or not name.strip():
            raise ValueError(f"Documentation impact rule {index} has no name.")
        if not isinstance(sources, list) or not sources or not all(isinstance(item, str) and item for item in sources):
            raise ValueError(f"Documentation impact rule '{name}' has invalid source_patterns.")
        if not isinstance(docs, list) or not docs or not all(isinstance(item, str) and item for item in docs):
            raise ValueError(f"Documentation impact rule '{name}' has invalid docs_any_of.")
        missing_docs = [item for item in docs if not (REPO_ROOT / item).is_file()]
        if missing_docs:
            raise ValueError(f"Documentation impact rule '{name}' references missing docs: {missing_docs}")

    return rules


def matching_files(changed: set[str], patterns: list[str]) -> set[str]:
    return {
        relative_path
        for relative_path in changed
        if any(fnmatch.fnmatchcase(relative_path, pattern) for pattern in patterns)
    }


def _read_pr_body() -> tuple[str | None, str | None]:
    """Return PR body or (None, error). Only for pull_request events."""
    if os.getenv("GITHUB_EVENT_NAME") != "pull_request":
        return None, None
    event_path = os.getenv("GITHUB_EVENT_PATH", "").strip()
    if not event_path:
        return None, None
    try:
        event = json.loads(Path(event_path).read_text(encoding="utf-8"))
        body = event.get("pull_request", {}).get("body") or ""
        return body, None
    except (AttributeError, OSError, json.JSONDecodeError):
        return None, "Kan de PR-beschrijving niet lezen om de docs-uitzondering te controleren."


def _is_checkbox_checked(body: str, label: str) -> bool:
    return bool(
        re.search(
            rf"(?im)^\s*-\s*\[[xX]\]\s*{re.escape(label)}\s*$",
            body,
        )
    )


def validate_docs_motivation(body: str) -> str | None:
    label = re.search(rf"(?im)^\s*{re.escape(DOCS_MOTIVATION_LABEL)}\s*(.*)$", body)
    if not label:
        return f"Vul '{DOCS_MOTIVATION_LABEL}' in bij de docs-uitzondering."
    tail = body[label.end() :]
    next_section = re.search(r"(?m)^##\s+", tail)
    motivation_block = tail[: next_section.start()] if next_section else tail
    motivation = f"{label.group(1)}\n{motivation_block}"
    motivation = re.sub(r"<!--.*?-->", "", motivation, flags=re.DOTALL).strip()
    if not re.search(r"[A-Za-zÀ-ÖØ-öø-ÿ0-9]", motivation):
        return f"Vul '{DOCS_MOTIVATION_LABEL}' inhoudelijk in bij de docs-uitzondering."
    return None


def validate_docs_checkboxes(findings: list[Finding], body: str | None) -> None:
    if body is None:
        return
    has_updated = _is_checkbox_checked(body, DOCS_UPDATED_CHECKBOX)
    has_no_docs = _is_checkbox_checked(body, NO_DOCS_CHECKBOX)
    if has_updated and has_no_docs:
        add(
            findings,
            ".github/pull_request_template.md",
            1,
            f"Kies exact één documentatie-optie: '{DOCS_UPDATED_CHECKBOX}' of '{NO_DOCS_CHECKBOX}', niet beide.",
        )
    elif not has_updated and not has_no_docs:
        add(
            findings,
            ".github/pull_request_template.md",
            1,
            f"Kies één documentatie-optie: '{DOCS_UPDATED_CHECKBOX}' of '{NO_DOCS_CHECKBOX}'.",
        )
    if has_no_docs:
        motivation_error = validate_docs_motivation(body)
        if motivation_error:
            add(
                findings,
                ".github/pull_request_template.md",
                1,
                motivation_error,
            )


def docs_impact_exemption() -> tuple[bool, str | None]:
    body, error = _read_pr_body()
    if error:
        return False, error
    if body is None:
        return False, None

    checked = _is_checkbox_checked(body, NO_DOCS_CHECKBOX)
    if not checked:
        return False, None

    motivation_error = validate_docs_motivation(body)
    if motivation_error:
        return False, motivation_error
    return True, None


def check_docs_impact(
    findings: list[Finding],
    changed: set[str],
    rules: list[dict[str, object]],
) -> None:
    missing: list[tuple[str, set[str], list[str]]] = []
    for rule in rules:
        name = str(rule["name"])
        source_patterns = list(rule["source_patterns"])
        docs_any_of = list(rule["docs_any_of"])
        source_changes = matching_files(changed, source_patterns)
        if source_changes and not matching_files(changed, docs_any_of):
            missing.append((name, source_changes, docs_any_of))

    if not missing:
        return

    exempt, exemption_error = docs_impact_exemption()
    if exempt:
        return
    # Motivatie-fout is al blocking via validate_docs_checkboxes (zie fix #2).
    # Geen aparte warning hier om dubbele melding te voorkomen; heuristiek-waarschuwingen hieronder blijven advisory.


    for name, source_changes, docs_any_of in missing:
        source = sorted(source_changes)[0]
        options = ", ".join(f"`{item}`" for item in docs_any_of)
        add(
            findings,
            source,
            1,
            f"{name} raakt gebruikersdocumentatie. Werk één van {options} bij, of vink "
            f"'{NO_DOCS_CHECKBOX}' aan en motiveer dit onder '{DOCS_MOTIVATION_LABEL}'.",
            severity="warning",
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--changed-only",
        action="store_true",
        help="Only evaluate checks relevant to files changed in this CI run.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Treat changed-file documentation impact warnings as errors.",
    )
    parser.add_argument(
        "--changed-file",
        action="append",
        default=[],
        help="Changed repository path for local testing; may be repeated.",
    )
    args = parser.parse_args()

    findings: list[Finding] = []
    changed = set(args.changed_file) if args.changed_file else changed_files_for_ci() if args.changed_only else set()
    try:
        docs_impact_rules = load_docs_impact_rules()
    except ValueError as exc:
        docs_impact_rules = []
        add(findings, ".github/docs-impact.json", 1, str(exc))

    # PR-template: documentatiekeuzes zijn exclusief (acceptatiecriteria #518).
    pr_body, _ = _read_pr_body()
    if pr_body is not None:
        validate_docs_checkboxes(findings, pr_body)

    companion_repo_url = "https://github.com/OpenQuatt/home-assistant-openquatt"
    docs_settings = REPO_ROOT / "docs/instellingen-en-meetwaarden.md"
    docs_tuning = REPO_ROOT / "docs/diagnose-en-afstelling.md"

    # 1) Known drift guard: flow mismatch threshold is compile-time, not runtime.
    flow_related = {
        "openquatt/oq_flow_control.yaml",
        "openquatt/oq_substitutions_common.yaml",
        "docs/instellingen-en-meetwaarden.md",
        "docs/diagnose-en-afstelling.md",
    }
    if not args.changed_only or any_changed(changed, flow_related):
        for rel in [
            "README.md",
            "docs/instellingen-en-meetwaarden.md",
            "docs/diagnose-en-afstelling.md",
        ]:
            path = REPO_ROOT / rel
            for ln in lines_with_phrase(path, "Flow mismatch threshold"):
                add(
                    findings,
                    rel,
                    ln,
                    "Use compile-time constants "
                    "`oq_flow_mismatch_threshold_lph` / `oq_flow_mismatch_hyst_lph` "
                    "instead of a runtime 'Flow mismatch threshold' entity.",
                )

        settings_text = read_text(docs_settings)
        if "oq_flow_mismatch_threshold_lph" not in settings_text:
            add(findings, "docs/instellingen-en-meetwaarden.md", 1, "Missing `oq_flow_mismatch_threshold_lph` reference.")
        if "oq_flow_mismatch_hyst_lph" not in settings_text:
            add(findings, "docs/instellingen-en-meetwaarden.md", 1, "Missing `oq_flow_mismatch_hyst_lph` reference.")
        if "oq_flow_mismatch_fallback_lph" in settings_text:
            add(findings, "docs/instellingen-en-meetwaarden.md", 1, "Obsolete `oq_flow_mismatch_fallback_lph` found.")

    # 2) Stable local entrypoints must point at the companion repository.
    home_assistant_docs = {
        "docs/dashboard/README.md",
        "docs/dashboardoverzicht.md",
    }
    if not args.changed_only or any_changed(changed, home_assistant_docs):
        for rel in sorted(home_assistant_docs):
            if companion_repo_url not in read_text(REPO_ROOT / rel):
                add(findings, rel, 1, f"Missing companion repository reference: {companion_repo_url}")

    # 3) Changed-file documentation impact guards.
    if args.changed_only and changed:
        check_docs_impact(findings, changed, docs_impact_rules)

    if not findings:
        print("Docs consistency checks passed.")
        return 0

    print(f"Docs consistency checks found {len(findings)} issue(s):")
    should_fail = False
    for f in findings:
        annotation = "error" if f.severity == "error" or args.strict else "warning"
        should_fail = should_fail or annotation == "error"
        print(f"::{annotation} file={f.file},line={f.line}::{f.message}")
        print(f"- {f.file}:{f.line} {f.message}")
    return 1 if should_fail else 0


if __name__ == "__main__":
    sys.exit(main())
