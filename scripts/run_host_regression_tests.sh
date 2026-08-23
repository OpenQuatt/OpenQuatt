#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="${repo_root}/tests/host"
build_root="$(mktemp -d "${TMPDIR:-/tmp}/openquatt-host-tests.XXXXXX")"
trap 'rm -rf "${build_root}"' EXIT

shopt -s nullglob
sources=("${test_root}"/*_test.cpp)
if (( ${#sources[@]} == 0 )); then
  echo "No host regression tests found in ${test_root}" >&2
  exit 1
fi

for source in "${sources[@]}"; do
  test_name="$(basename "${source}" .cpp)"
  binary="${build_root}/${test_name}"
  echo "[build] ${test_name}"
  "${CXX:-c++}" \
    -std=c++17 \
    -Wall \
    -Wextra \
    -Werror \
    -I"${repo_root}" \
    -I"${repo_root}/openquatt" \
    "${source}" \
    -o "${binary}"
  echo "[run] ${test_name}"
  "${binary}"
done

echo "[run] energy history memory contract"
python3 "${repo_root}/scripts/tests/test_energy_history_memory_contract.py"

echo "[run] incident manager action contract"
python3 "${repo_root}/scripts/tests/test_incident_manager_action_contract.py"

echo "[run] internal heap placement contract"
python3 "${repo_root}/scripts/tests/test_internal_heap_contract.py"

echo "[run] OTB polling lifecycle contract"
python3 "${repo_root}/scripts/tests/test_otb_polling_lifecycle_contract.py"

echo "[run] boiler commissioning ownership contract"
python3 "${repo_root}/scripts/tests/test_boiler_commissioning_ownership_contract.py"

echo "[run] MQTT ingress lifecycle contract"
python3 "${repo_root}/scripts/tests/test_mqtt_ingress_lifecycle_contract.py"

echo "[run] crash snapshot lifecycle contract"
python3 "${repo_root}/scripts/tests/test_crash_snapshot_lifecycle_contract.py"

echo "[run] usage telemetry cleanup lifecycle failure injection"
python3 "${repo_root}/scripts/tests/test_usage_telemetry_cleanup_lifecycle.py"

echo "Host regression tests passed (${#sources[@]})."
