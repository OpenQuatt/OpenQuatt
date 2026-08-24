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

# Python-contracten (scripts/tests/test_*.py) draaien separaat via `python-contracts` job / `npm run check:python-contracts` (zie #518).
echo "Host regression tests passed (${#sources[@]})."
