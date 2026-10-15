#!/usr/bin/env bash
# Usage: ./build.sh [idf.py args...]   e.g. ./build.sh build flash monitor
# Needs ESP-IDF 5.4 or 5.5 exported (LiveKit's SDK is tested on those).
set -euo pipefail
cd "$(dirname "$0")"
[ -f secrets.defaults ] || { echo "Create secrets.defaults from secrets.defaults.example first"; exit 1; }
exec idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;secrets.defaults" "$@"
