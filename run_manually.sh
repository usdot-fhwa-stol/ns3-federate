#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
port="${1:-5011}"
cmdport="${2:-0}"
ns3Version="${NS3_VERSION:-3.42}"

cd "$SCRIPT_DIR"
rm -f ClientServerChannelMessages.pb.h ClientServerChannelMessages.pb.cc
NS3_VERSION="$ns3Version" ./premake5 gmake --generate-protobuf
NS3_VERSION="$ns3Version" make -j1 config=debug

export LD_LIBRARY_PATH="${SCRIPT_DIR}/ns-allinone-${ns3Version}/ns-${ns3Version}/build/lib:${LD_LIBRARY_PATH:-}"
exec "${SCRIPT_DIR}/bin/Debug/ns3-federate" --port="$port" --cmdPort="$cmdport" --configFile="${SCRIPT_DIR}/ns3_federate_config.xml"
