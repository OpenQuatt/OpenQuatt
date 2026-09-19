#!/usr/bin/env bash
# In-container OpenQuatt build and test runner.
#
# Runs the repository's host-side build and test suite (mirroring the CI build
# workflow) and, on request, the ESPHome firmware validation/compilation.
#
# Modes:
#   ci       Host CI-equivalent build and tests (default; no ESP-IDF toolchain).
#   firmware Additionally bootstrap the ESPHome venv and validate all firmware
#            configs (dev.py validate --config-only). Extra args are forwarded.
#   compile  Additionally bootstrap the ESPHome venv and full dev.py validate,
#            which compiles every enabled profile. This downloads the ESP-IDF
#            toolchain on first run (several GB) and is slow.
#   shell    Start an interactive shell inside the container.
set -euo pipefail

ROOT_DIR="/workspace"
VENV_DIR="/home/dev/openquatt-venv"
cd "${ROOT_DIR}"

MODE="${1:-ci}"
shift || true

step() {
    echo ""
    echo "================================================================"
    echo "[openquatt-dev] $*"
    echo "================================================================"
}

host_checks() {
    step "Install web build dependencies (npm ci)"
    npm ci

    step "Settings-backup consistency"
    node openquatt/web/check-settings-backup.mjs

    step "Python contract tests"
    npm run check:python-contracts

    step "Docs consistency"
    npm run check:docs

    step "C/C++ formatting check"
    npm run check:cpp-format

    step "Web build, tests and quality"
    npm run check:web

    step "Web smoke checks"
    npm run smoke:web

    step "HIL harness tests"
    npm run check:hil

    step "Host regression tests (C++)"
    ./scripts/run_host_regression_tests.sh
}

esphome_bootstrap() {
    if [ -x "${VENV_DIR}/bin/esphome" ]; then
        echo "[openquatt-dev] ESPHome venv present, skipping bootstrap (${VENV_DIR})."
        return
    fi
    step "Bootstrap ESPHome virtual environment"
    python3 scripts/dev.py bootstrap --venv-dir "${VENV_DIR}"
}

case "${MODE}" in
    ci|test)
        host_checks
        ;;
    firmware)
        host_checks
        esphome_bootstrap
        step "Validate firmware configs (config-only)"
        python3 scripts/dev.py validate --config-only --venv-dir "${VENV_DIR}" "$@"
        ;;
    compile|full)
        host_checks
        esphome_bootstrap
        step "Validate and compile all firmware profiles"
        echo "[openquatt-dev] Note: this downloads the ESP-IDF toolchain on first run."
        python3 scripts/dev.py validate --venv-dir "${VENV_DIR}" "$@"
        ;;
    shell)
        exec bash "$@"
        ;;
    *)
        echo "Unknown mode: ${MODE}" >&2
        echo "Usage: $0 [ci|firmware|compile|shell] [dev.py args]" >&2
        exit 2
        ;;
esac

step "All checks passed."