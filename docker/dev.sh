#!/usr/bin/env bash
# Build the OpenQuatt development image and run the in-container build/tests.
#
# Usage:
#   docker/dev.sh [--rebuild] [ci|firmware|compile|shell] [extra args...]
#
#   --rebuild  Rebuild the image even if one already exists.
#   ci         Host CI-equivalent build and tests (default).
#   firmware   ci plus ESPHome config validation for all enabled profiles.
#   compile    ci plus full firmware compilation (slow; downloads ESP-IDF).
#   shell      Interactive shell inside the container.
#
# The repository is mounted at /workspace and runs as your host UID/GID, so
# build artifacts are owned by you. User-local caches (npm, ESP-IDF tools,
# ccache, the ESPHome venv) persist in the named Docker volume
# openquatt-dev-home. Override image/volume names with OQ_DEV_IMAGE and
# OQ_DEV_HOME_VOLUME.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="${OQ_DEV_IMAGE:-openquatt-dev}"
HOME_VOLUME="${OQ_DEV_HOME_VOLUME:-openquatt-dev-home}"

REBUILD=0
if [[ "${1:-}" == "--rebuild" || "${1:-}" == "-r" ]]; then
    REBUILD=1
    shift
fi

MODE="${1:-ci}"
shift || true

if [[ "${MODE}" == "help" || "${MODE}" == "-h" || "${MODE}" == "--help" ]]; then
    echo "Usage: $0 [--rebuild] [ci|firmware|compile|shell] [extra args...]"
    echo ""
    echo "  --rebuild  Rebuild the image even if one already exists."
    echo "  ci         Host CI-equivalent build and tests (default)."
    echo "  firmware   ci plus ESPHome config validation for all enabled profiles."
    echo "  compile    ci plus full firmware compilation (slow; downloads ESP-IDF)."
    echo "  shell      Interactive shell inside the container."
    echo ""
    echo "Environment: OQ_DEV_IMAGE (default openquatt-dev)"
    echo "             OQ_DEV_HOME_VOLUME (default openquatt-dev-home)"
    exit 0
fi

cd "${ROOT_DIR}"

if [[ "${REBUILD}" == "1" ]] || ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
    echo "[openquatt-dev] Building image ${IMAGE_NAME} ..."
    docker build \
        --build-arg "CLANG_FORMAT_VERSION=$(cat .clang-format-version)" \
        -t "${IMAGE_NAME}" \
        -f "docker/Dockerfile" \
        "${ROOT_DIR}"
fi

if ! docker volume inspect "${HOME_VOLUME}" >/dev/null 2>&1; then
    echo "[openquatt-dev] Creating volume ${HOME_VOLUME} ..."
    docker volume create "${HOME_VOLUME}"
fi

TTY_FLAGS=()
if [[ -t 0 ]]; then
    TTY_FLAGS=(-it)
fi

exec docker run --rm \
    "${TTY_FLAGS[@]}" \
    --name openquatt-dev-run \
    -u "$(id -u):$(id -g)" \
    -e "HOME=/home/dev" \
    -v "${ROOT_DIR}:/workspace" \
    -v "${HOME_VOLUME}:/home/dev" \
    -w /workspace \
    "${IMAGE_NAME}" \
    bash /workspace/docker/ci.sh "${MODE}" "$@"