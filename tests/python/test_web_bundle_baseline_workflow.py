"""Exercise the real baseline step against an isolated shallow Git checkout."""

import os
from pathlib import Path
import tempfile
import textwrap
import unittest

from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/ci-build.yml").read_text()
STEP = WORKFLOW.split("      - name: Build exact PR base bundles for size comparison\n", 1)[1]
SHELL = textwrap.dedent(STEP.split("        run: |\n", 1)[1].split("\n      - name:", 1)[0])


class WebBundleBaselineWorkflowTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="oq-web-baseline-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.environment = {
            **{key: value for key, value in os.environ.items() if not key.startswith("GIT_")},
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": os.devnull,
            "GIT_TERMINAL_PROMPT": "0",
            "GIT_ALLOW_PROTOCOL": "file",
        }
        remote = self.root / "origin"
        remote.mkdir()
        self.git("init", "--initial-branch=dev", cwd=remote)
        self.git("config", "user.name", "OpenQuatt fixture", cwd=remote)
        self.git("config", "user.email", "fixture@example.invalid", cwd=remote)
        marker = remote / "openquatt/web/marker.txt"
        marker.parent.mkdir(parents=True)
        for version in range(4):
            marker.write_text(f"version {version}\n")
            self.git("add", "openquatt/web/marker.txt", cwd=remote)
            self.git("commit", "--message", f"version {version}", cwd=remote)
            if version == 0:
                self.base_sha = self.git("rev-parse", "HEAD", cwd=remote)
        self.checkout = self.root / "checkout"
        self.git("clone", "--depth=2", remote.as_uri(), str(self.checkout), cwd=self.root)
        self.head_sha = self.git("rev-parse", "HEAD", cwd=self.checkout)
        missing = run_process(
            ["git", "cat-file", "-e", f"{self.base_sha}^{{commit}}"],
            cwd=self.checkout, env=self.environment, capture_output=True, text=True, timeout=15,
        )
        self.assertNotEqual(0, missing.returncode, "Fixture must omit the PR base from its shallow checkout")
        binary_directory = self.root / "bin"
        binary_directory.mkdir()
        npm = binary_directory / "npm"
        npm.write_text('#!/bin/sh\nprintf \'%s\\n\' "$*" >> "$NPM_CALLS"\n')
        npm.chmod(0o755)
        self.npm_calls = self.root / "npm-calls"
        self.github_env = self.root / "github-env"
        self.runner_temp = self.root / "runner-temp"
        self.runner_temp.mkdir()
        self.environment.update({
            "PATH": f"{binary_directory}{os.pathsep}{os.environ['PATH']}",
            "NPM_CALLS": str(self.npm_calls),
            "GITHUB_ENV": str(self.github_env),
            "RUNNER_TEMP": str(self.runner_temp),
        })

    def git(self, *arguments, cwd):
        result = run_process(
            ["git", *arguments], cwd=cwd, env=self.environment,
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        return result.stdout.strip()

    def run_baseline(self, base_sha):
        result = run_process(
            ["bash", "-e", "-o", "pipefail", "-c", SHELL],
            cwd=self.checkout, env={**self.environment, "BASE_SHA": base_sha},
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(self.head_sha, self.git("rev-parse", "HEAD", cwd=self.checkout))
        return result

    def test_fetches_exact_older_base_before_archiving(self):
        result = self.run_baseline(self.base_sha)
        self.assertEqual(0, result.returncode, result.stderr)
        baseline = self.runner_temp / "openquatt-web-baseline"
        self.assertEqual("version 0\n", (baseline / "openquatt/web/marker.txt").read_text())
        self.assertEqual(2, len(self.npm_calls.read_text().splitlines()))
        self.assertEqual(
            f"OPENQUATT_WEB_BUNDLE_BASE_DIR={baseline}/openquatt/web\n",
            self.github_env.read_text(),
        )

    def test_missing_remote_base_fails_before_package_install(self):
        result = self.run_baseline("0" * 40)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse(self.npm_calls.exists())
        self.assertFalse(self.github_env.exists())


if __name__ == "__main__":
    unittest.main()
