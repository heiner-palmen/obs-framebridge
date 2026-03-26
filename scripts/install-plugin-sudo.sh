#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/install-plugin-sudo.sh [BUILD_DIR] [CONFIG]
# Defaults: BUILD_DIR=build, CONFIG=Debug

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Accept either an absolute build dir or a path relative to project root
_raw_build_dir="${1:-build}"
if [[ "${_raw_build_dir}" = /* ]]; then
	BUILD_DIR="${_raw_build_dir}"
else
	BUILD_DIR="${PROJECT_ROOT}/${_raw_build_dir}"
fi

CONFIG="${2:-Debug}"
CMAKE="$(command -v cmake || echo /usr/bin/cmake)"

echo "Building project in: ${BUILD_DIR} (config: ${CONFIG})"
"${CMAKE}" --build "${BUILD_DIR}" --config "${CONFIG}" || { echo "Build failed" >&2; exit 1; }

echo "Installing (may prompt for sudo password)..."
# Run install under sudo so copying to system plugin directories succeeds
sudo "${CMAKE}" --install "${BUILD_DIR}" --config "${CONFIG}" || { echo "Install failed" >&2; exit 1; }

echo "Install completed."
