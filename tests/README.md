# Test environment

The local runner and CI use the same test discovery, compiler settings and
failure policy. A regression test should fail before a bug fix and pass after
it. Preserve observable behaviour and safety boundaries when reorganizing code.

## Setup

Install Python 3.12 or newer, Node.js 24.x (the reference version is in
`.node-version`), npm, a C++ compiler and `jq`. On macOS, install the Xcode
Command Line Tools for the compiler and SDK, and `brew install jq` if needed.
Then, from the repository root:

```sh
python3 scripts/dev.py bootstrap
source .venv/bin/activate
npm ci --ignore-scripts
npm test
```

Bootstrap creates or replaces `.venv` with the ESPHome version pinned in
`.github/requirements-esphome.txt`. Activate that environment before running
Python tests or npm commands that invoke them. The runner reports a missing or
incorrect ESPHome version before starting the suites; it does not silently
substitute a different interpreter or install dependencies.

## Layout and ownership

| Location | Purpose |
| --- | --- |
| `tests/host/*_test.cpp` | Executable C++ control, safety and policy regressions |
| `tests/python/test_*.py` | Tooling, YAML/codegen contracts and component runtime harnesses |
| `tests/fixtures/` | Peripheral stubs and C++ harness inputs |
| `tests/support/cpp.py` | Shared compiler, SDK, assertion, sanitizer and timeout policy |
| `tests/support/process.py` | Bounded command execution and process-group cleanup on macOS/Linux |
| `tests/hil/*.test.mjs` | Hardware-independent tests of the HIL harness |
| `tests/hil/scenarios/` | Scenario definitions and instructions for separate physical HIL runs |
| `openquatt/web/tests/*.test.mjs` | Web logic tests, kept beside their owning application |
| `scripts/run_tests.py` | Common local and CI entrypoint |

Use `snake_case` for Python tests and C++ host tests, with `test_*.py` and
`*_test.cpp` discovery suffixes. Web and HIL tests follow the existing
`*.test.mjs` convention. Fixtures belong to the test that needs them; shared
support should contain infrastructure rather than copies of production logic.

## Commands

```sh
npm test                          # All hardware-independent suites
npm run check:host                # C++ host regressions
npm run check:python              # Complete Python suite, including ESPHome integration
npm run check:hil                 # HIL harness tests without physical hardware
npm run check:web                 # Web build, tests and size/encoding checks
npm run check:sanitizers          # C++ host tests and Python harnesses with ASan + UBSan
```

For focused work and discovery:

```sh
python3 scripts/run_tests.py host --pattern '*supervisory*'
python3 scripts/run_tests.py python --pattern 'test_nvs_*.py'
python3 scripts/run_tests.py --list
python3 scripts/run_tests.py host python --sanitize
```

Quote patterns so the shell does not expand them. A pattern that selects no
tests fails. `--list` only inventories files and requires no compiler or ESPHome.
Tests left in the former `scripts/tests/` directory also fail discovery, so an
older feature branch cannot silently add regressions outside the active suite.
The existing `scripts/run_host_regression_tests.sh` and
`npm run check:python-contracts` remain compatible entrypoints.

All C++ harnesses use `tests/support/cpp.py`: C++17, `-Wall -Wextra -Werror`,
assertions enabled, and macOS SDK headers where required. A harness may request
an explicit newer language standard when its production source needs it.
`CXX` can select a compiler or wrapper, for example `CXX='ccache clang++'`;
`SDKROOT` can select the macOS SDK. Compilation is sequential, with a 120-second
timeout per invocation and a 30-second runtime timeout by default. Specific
harnesses may use shorter bounds. Binaries use temporary directories that are
cleaned up on failure as well as success.
Timeouts and interrupts stop the complete command process group, including
compiler wrappers, npm shells and Node workers, before temporary files are
removed. The shared process helper also preserves exit status and diagnostics.

Sanitizers use `OPENQUATT_TEST_SANITIZERS=address,undefined`; empty or `none`
disables them. Unknown values fail. Compiler errors, nonzero test exits,
timeouts, empty suites and skipped Python tests all fail the canonical runner.
CI installs the pinned ESPHome version so integration tests actually execute.
The Python runner builds the web assets before discovery because real ESPHome
package composition validates their file paths, including on a fresh checkout.
ASan and UBSan run in a separate required job before firmware builds.

## Keeping tests useful

- Add a behavioural regression for the defect or invariant being protected,
  including relevant invalid input, stale data, retry, timing and failure paths.
- Keep source contracts for properties requiring integration checks, such as
  YAML/codegen wiring, persistent entity IDs and package composition. Prefer
  parsing or executable composition over incidental formatting checks.
- Avoid historical line-count ceilings and checks that merely search another
  test's source for function names. Executing that test supplies stronger proof.
- Consolidate overlapping scenarios only when their unique assertions remain
  covered. Test count is not a quality target.
- Use the shared compiler support for Python C++ harnesses. Prefer compiling
  production headers or components with peripheral stubs to extracting methods
  from source text. Existing extraction harnesses remain until an equivalent
  component boundary can be tested without changing firmware behaviour.

## Validation boundaries

`npm test` exercises software on the development machine. ESPHome config
validation and firmware compilation remain separate CI integration checks.
Physical protocol timing, actuators and runtime ESP32 heap/stack margins still
require representative hardware and HIL evidence; desktop sanitizer results do
not establish those margins. See [HIL testing](../docs/development/hil-testing.md)
and [development on macOS](../docs/development/ontwikkelen-op-mac.md).
