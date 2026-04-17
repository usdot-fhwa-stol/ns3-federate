#!/bin/bash
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.38}"
NS3_TARBALL="ns-allinone-${NS3_VERSION}.tar.bz2"
NS3_URL="https://www.nsnam.org/releases/${NS3_TARBALL}"
PREMAKE_URL="https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz"
REPO_ROOT="/opt/ns3-federate"
NR_REPO="${NR_REPO:-https://gitlab.com/cttc-lena/nr.git}"
NR_BRANCH="${NR_BRANCH:-5g-lena-v2.4.y}"
NS3_ALLINONE_DIR="${REPO_ROOT}/ns-allinone-${NS3_VERSION}"
NS3_DIR="${NS3_ALLINONE_DIR}/ns-${NS3_VERSION}"

export DEBIAN_FRONTEND=noninteractive

echo "=== Installing base dependencies ==="
apt-get update
apt-get install -y --no-install-recommends \
  build-essential gcc g++ make pkg-config lbzip2 ca-certificates curl git \
  libprotobuf-dev libsqlite3-dev libxml2-dev libgsl-dev libc6-dev libeigen3-dev \
  patch protobuf-compiler python3 python3-pip rsync unzip wget cmake sqlite3 nano
rm -rf /var/lib/apt/lists/*

cd "$REPO_ROOT"

echo "=== Downloading ${NS3_TARBALL} ==="
if [[ ! -f "$REPO_ROOT/${NS3_TARBALL}" ]]; then
  wget -q "$NS3_URL" -O "$REPO_ROOT/${NS3_TARBALL}"
fi

if [[ ! -d "$NS3_ALLINONE_DIR" ]]; then
  echo "=== Extracting ${NS3_TARBALL} ==="
  tar -xf "$REPO_ROOT/${NS3_TARBALL}" -C "$REPO_ROOT"
fi

echo "=== Downloading premake5 ==="
if [[ ! -x "$REPO_ROOT/premake5" ]]; then
  wget -q "$PREMAKE_URL" -O /tmp/premake.tar.gz
  tar -xzf /tmp/premake.tar.gz -C "$REPO_ROOT"
  rm -f /tmp/premake.tar.gz
fi

cd "$NS3_DIR"

if [[ ! -f ".ns3_federate_patch_applied" ]]; then
  echo "=== Patching ns-3 ${NS3_VERSION} ==="
  patch --strip=1 --input="$REPO_ROOT/patches/ns3-lte.patch"
  touch .ns3_federate_patch_applied
fi

# =========================================================
# 🔥 Patch LteUeMac log level (ERROR -> DEBUG)
# =========================================================
echo "=== Patching LteUeMac log level ==="

FILE="$(find "$NS3_DIR" -path '*/src/lte/model/lte-ue-mac.cc' | head -n 1)"

if [[ -z "$FILE" ]]; then
  echo "ERROR: lte-ue-mac.cc not found"
  exit 1
fi

echo "Found: $FILE"

echo "Before patch:"
grep -n "No active flows for this UL-DCI" "$FILE" || true

sed -i 's/NS_LOG_ERROR("No active flows for this UL-DCI");/NS_LOG_DEBUG("No active flows for this UL-DCI");/' "$FILE"

echo "After patch:"
grep -n "No active flows for this UL-DCI" "$FILE" || true
# =========================================================

echo "=== Installing NR ${NR_BRANCH} ==="
rm -rf "$NS3_DIR/contrib/nr"
git clone --branch "$NR_BRANCH" --depth 1 "$NR_REPO" "$NS3_DIR/contrib/nr"

echo "=== Cleaning ns-3 build ==="
if [[ -x "$NS3_DIR/ns3" ]]; then
  "$NS3_DIR/ns3" clean || true
fi
rm -rf "$NS3_DIR/build"

echo "=== Configuring ns-3 ${NS3_VERSION} with NR ==="
cd "$NS3_DIR"
CXXFLAGS="-Wno-error" ./ns3 configure --enable-examples --enable-tests

echo "=== Building ns-3 ${NS3_VERSION} with NR ==="
CXXFLAGS="-Wno-error" ./ns3 build -j"$(nproc)"

echo "=== Verifying NR build artifacts ==="
find "$NS3_DIR/build" \( -name '*nr*' -o -path '*/nr/*' \) | sort || true

echo "=== Optional NR smoke test ==="
./ns3 run cttc-nr-demo || true

echo "=== Generating protobuf sources ==="
cd "$REPO_ROOT"
rm -f ClientServerChannelMessages.pb.h ClientServerChannelMessages.pb.cc
./premake5 gmake --generate-protobuf

echo "=== Checking premake-generated NR include paths ==="
grep -n "contrib/nr/helper" ns3-federate.make || true
grep -n "contrib/nr/model" ns3-federate.make || true
grep -n "contrib/nr/utils" ns3-federate.make || true
ls -l "$NS3_DIR/contrib/nr/helper/nr-helper.h"
ls -l "$NS3_DIR/contrib/nr/helper/cc-bwp-helper.h"

echo "=== Building ns3-federate ==="
make config=debug clean
make -j1 config=debug

if [[ -f "$REPO_ROOT/bin/Debug/ns3-federate" ]]; then
  cp -f "$REPO_ROOT/bin/Debug/ns3-federate" "$REPO_ROOT/ns3-federate"
fi

chmod +x "$REPO_ROOT/run.sh" "$REPO_ROOT/run_from_mosaic.sh" "$REPO_ROOT/run_manually.sh" "$REPO_ROOT/premake5"
mkdir -p "$REPO_ROOT/ns3config"
chmod 777 "$REPO_ROOT/ns3config"

echo "=== install_dependencies.sh complete ==="