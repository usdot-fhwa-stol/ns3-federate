#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
port="${1:-40001}"
cmdport="${2:-0}"
ns3Version="3.38"

export LD_LIBRARY_PATH="${SCRIPT_DIR}/ns-allinone-${ns3Version}/ns-${ns3Version}/build/lib:${LD_LIBRARY_PATH:-}"
exec "${SCRIPT_DIR}/ns3-federate" --port="$port" --cmdPort="$cmdport" --configFile="${SCRIPT_DIR}/ns3config/ns3_federate_config.xml"
