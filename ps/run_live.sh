#!/usr/bin/env bash
# More tolerant of moderate lighting/shadow changes within the same blob.
# Later command-line options can override this live preset.
set -euo pipefail
exec "$(dirname "$0")/camera_udp" --tolerance 52 --edge-threshold 36 --primary-only "$@"
