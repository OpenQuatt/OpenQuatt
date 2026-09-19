# OpenQuatt development environment.
#
# Provides the toolchain needed to build and test this repository:
#   - Python 3.12 (required by ESPHome 2026.9; see scripts/dev.py MIN_BOOTSTRAP_PYTHON)
#   - Node.js 22 (same major version as CI, see .github/workflows/esphome-build.yml)
#   - C++ compiler + make (host regression tests in tests/host)
#   - ccache (used by scripts/dev.py when compiling firmware profiles)
#   - the exact clang-format pinned in .clang-format-version
#
# Build and run via docker/dev.sh, e.g.:
#   docker/dev.sh          # host CI-equivalent build+tests
#   docker/dev.sh shell    # interactive development shell
# The base is pinned to bookworm so the NodeSource Node.js 22 repo is
# deterministic across rebuilds.
FROM python:3.12-slim-bookworm

ARG CLANG_FORMAT_VERSION=22.1.8

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PATH="/opt/clang-format-venv/bin:${PATH}" \
    HOME=/home/dev

RUN apt-get update && apt-get install -y --no-install-recommends \
        bzip2 \
        ca-certificates \
        ccache \
        curl \
        git \
        build-essential \
        unzip \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Node.js 22 (LTS), the same major version used by CI (actions/setup-node@v6).
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/* \
    && node --version \
    && npm --version

# The repository pins clang-format in .clang-format-version; install that exact
# binary into its own virtualenv so `npm run check:cpp-format` passes.
RUN python3 -m venv /opt/clang-format-venv \
    && /opt/clang-format-venv/bin/pip install "clang-format==${CLANG_FORMAT_VERSION}" \
    && /opt/clang-format-venv/bin/clang-format --version

# Home directory for user-local state (npm cache, ESP-IDF tools, ccache, the
# ESPHome venv). docker/dev.sh exposes it as a named volume and runs the
# container as the host UID, so it is made world-writable here.
RUN useradd --create-home --uid 1000 --user-group dev \
    && chmod 0777 /home/dev

ENV ESPHOME_ESP_IDF_PREFIX=/home/dev/.cache/esphome/idf

WORKDIR /workspace