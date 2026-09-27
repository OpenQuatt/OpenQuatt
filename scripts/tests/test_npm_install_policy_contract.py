import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
NPMRC = (ROOT / ".npmrc").read_text()
LOCKFILE = json.loads((ROOT / "package-lock.json").read_text())
WORKFLOW_DIR = ROOT / ".github/workflows"

# A change here is a security-review decision, not routine lockfile maintenance.
EXPECTED_INSTALL_SCRIPT_PACKAGES = {"node_modules/esbuild"}


class NpmInstallPolicyContractTest(unittest.TestCase):
    def test_repository_disables_dependency_lifecycle_scripts(self):
        settings = {
            line.split("=", 1)[0].strip(): line.split("=", 1)[1].strip()
            for line in NPMRC.splitlines()
            if line.strip() and not line.lstrip().startswith("#") and "=" in line
        }
        self.assertEqual(settings.get("ignore-scripts"), "true")

    def test_ci_installs_dependencies_without_lifecycle_scripts(self):
        offenders = []
        npm_ci_lines = 0
        for workflow in sorted(WORKFLOW_DIR.glob("*.yml")):
            for line_number, line in enumerate(workflow.read_text().splitlines(), start=1):
                if "npm ci" not in line or line.lstrip().startswith("#"):
                    continue
                npm_ci_lines += 1
                if "--ignore-scripts" not in line:
                    offenders.append(f"{workflow.relative_to(ROOT)}:{line_number}: {line.strip()}")
        self.assertGreater(npm_ci_lines, 0, "No npm ci install steps found in workflows")
        self.assertEqual(offenders, [], "Unsafe npm ci step(s):\n" + "\n".join(offenders))

    def test_lockfile_install_scripts_are_explicitly_reviewed(self):
        actual = {
            path
            for path, package in LOCKFILE.get("packages", {}).items()
            if isinstance(package, dict) and package.get("hasInstallScript") is True
        }
        self.assertEqual(actual, EXPECTED_INSTALL_SCRIPT_PACKAGES)


if __name__ == "__main__":
    unittest.main()
