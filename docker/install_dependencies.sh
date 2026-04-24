#!/bin/bash
set -euo pipefail

REPO_ROOT="/opt/ns3-federate"

# Build mode:
#   nr  -> legacy 5G-LENA on ns-3 release tarball
#   v2x -> CTTC NR V2X pairing based on ns-3-dev + nr V2X tags/branches
NS3_FLAVOR="${NS3_FLAVOR:-v2x}"

# Legacy NR defaults
LEGACY_NS3_VERSION="${LEGACY_NS3_VERSION:-${NS3_VERSION:-3.38}}"
LEGACY_NR_REPO="${LEGACY_NR_REPO:-https://gitlab.com/cttc-lena/nr.git}"
LEGACY_NR_BRANCH="${LEGACY_NR_BRANCH:-5g-lena-v2.4.y}"

# NR V2X defaults (matching the CTTC README matrix for v2x-v1.1)
V2X_NS3_VERSION="${V2X_NS3_VERSION:-${NS3_VERSION:-3.42}}"
V2X_NS3_REPO="${V2X_NS3_REPO:-https://gitlab.com/cttc-lena/ns-3-dev.git}"
V2X_NS3_REF="${V2X_NS3_REF:-ns-3-dev-v2x-v1.1}"
V2X_NR_REPO="${V2X_NR_REPO:-https://gitlab.com/cttc-lena/nr.git}"
V2X_NR_REF="${V2X_NR_REF:-v2x-1.1}"

PREMAKE_URL="https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz"

if [[ "$NS3_FLAVOR" == "v2x" ]]; then
  NS3_VERSION="$V2X_NS3_VERSION"
  NS3_ALLINONE_DIR="${REPO_ROOT}/ns-allinone-${NS3_VERSION}"
  NS3_DIR="${NS3_ALLINONE_DIR}/ns-${NS3_VERSION}"
  NS3_BUILD_MODE="cmake"
else
  NS3_VERSION="$LEGACY_NS3_VERSION"
  NS3_ALLINONE_DIR="${REPO_ROOT}/ns-allinone-${NS3_VERSION}"
  NS3_DIR="${NS3_ALLINONE_DIR}/ns-${NS3_VERSION}"
  NS3_BUILD_MODE="release-tarball"
fi

export DEBIAN_FRONTEND=noninteractive

echo "=== Installing base dependencies ==="
apt-get update
apt-get install -y --no-install-recommends \
  build-essential gcc g++ make pkg-config lbzip2 ca-certificates curl git \
  libprotobuf-dev libsqlite3-dev libxml2-dev libgsl-dev libc6-dev libeigen3-dev \
  patch protobuf-compiler python3 python3-pip rsync unzip wget cmake sqlite3 nano
rm -rf /var/lib/apt/lists/*

cd "$REPO_ROOT"

prepare_legacy_tree() {
  local ns3_tarball="ns-allinone-${LEGACY_NS3_VERSION}.tar.bz2"
  local ns3_url="https://www.nsnam.org/releases/${ns3_tarball}"

  echo "=== Downloading ${ns3_tarball} ==="
  if [[ ! -f "$REPO_ROOT/${ns3_tarball}" ]]; then
    wget -q "$ns3_url" -O "$REPO_ROOT/${ns3_tarball}"
  fi

  if [[ ! -d "$NS3_ALLINONE_DIR" ]]; then
    echo "=== Extracting ${ns3_tarball} ==="
    tar -xf "$REPO_ROOT/${ns3_tarball}" -C "$REPO_ROOT"
  fi

  echo "=== Installing NR ${LEGACY_NR_BRANCH} ==="
  rm -rf "$NS3_DIR/contrib/nr"
  git clone --branch "$LEGACY_NR_BRANCH" --depth 1 "$LEGACY_NR_REPO" "$NS3_DIR/contrib/nr"
}

prepare_v2x_tree() {
  echo "=== Preparing CTTC NR V2X source tree ==="
  mkdir -p "$NS3_ALLINONE_DIR"

  if [[ ! -d "$NS3_DIR/.git" ]]; then
    rm -rf "$NS3_DIR"
    git clone "$V2X_NS3_REPO" "$NS3_DIR"
  fi

  cd "$NS3_DIR"
  git fetch --tags origin
  git checkout "tags/${V2X_NS3_REF}" -B "${V2X_NS3_REF}-branch"

  mkdir -p contrib
  rm -rf contrib/nr
  git clone "$V2X_NR_REPO" contrib/nr
  cd contrib/nr
  git fetch --tags origin
  git checkout "tags/${V2X_NR_REF}" -B "${V2X_NR_REF}-branch"
  cd "$NS3_DIR"
}

echo "=== Downloading premake5 ==="
if [[ ! -x "$REPO_ROOT/premake5" ]]; then
  wget -q "$PREMAKE_URL" -O /tmp/premake.tar.gz
  tar -xzf /tmp/premake.tar.gz -C "$REPO_ROOT"
  rm -f /tmp/premake.tar.gz
fi

if [[ "$NS3_FLAVOR" == "v2x" ]]; then
  prepare_v2x_tree
else
  prepare_legacy_tree
fi

cd "$NS3_DIR"

if [[ -f "$REPO_ROOT/patches/ns3-lte.patch" ]] && [[ ! -f ".ns3_federate_patch_applied" ]]; then
  PATCH_TARGET="src/lte/helper/no-backhaul-epc-helper.cc"
  if [[ -f "$PATCH_TARGET" ]]; then
    echo "=== Patching ns-3 LTE helper ==="
    patch --forward --strip=1 --input="$REPO_ROOT/patches/ns3-lte.patch" || true
    touch .ns3_federate_patch_applied
  else
    echo "=== Skipping LTE patch: ${PATCH_TARGET} not found in selected tree ==="
  fi
fi

echo "=== Patching LteUeMac log level when available ==="
LTE_MAC_FILE="$(find "$NS3_DIR" -path '*/src/lte/model/lte-ue-mac.cc' | head -n 1 || true)"
if [[ -n "$LTE_MAC_FILE" ]]; then
  grep -n "No active flows for this UL-DCI" "$LTE_MAC_FILE" || true
  sed -i 's/NS_LOG_ERROR("No active flows for this UL-DCI");/NS_LOG_DEBUG("No active flows for this UL-DCI");/' "$LTE_MAC_FILE"
  grep -n "No active flows for this UL-DCI" "$LTE_MAC_FILE" || true
else
  echo "=== Skipping LteUeMac patch: file not present ==="
fi

echo "=== Cleaning ns-3 build ==="
if [[ -x "$NS3_DIR/ns3" ]]; then
  "$NS3_DIR/ns3" clean || true
fi
rm -rf "$NS3_DIR/build"

echo "=== Configuring ns-3 ${NS3_VERSION} (${NS3_FLAVOR}) ==="
cd "$NS3_DIR"
CXXFLAGS="-Wno-error" ./ns3 configure --enable-examples --enable-tests

echo "=== Building ns-3 ${NS3_VERSION} (${NS3_FLAVOR}) ==="
CXXFLAGS="-Wno-error" ./ns3 build -j"$(nproc)"

echo "=== Verifying NR build artifacts ==="
find "$NS3_DIR/build" \( -name '*nr*' -o -path '*/nr/*' \) | sort || true

echo "=== Smoke test ==="
if [[ "$NS3_FLAVOR" == "v2x" ]]; then
  ./ns3 run cttc-nr-v2x-demo-simple || true
else
  ./ns3 run cttc-nr-demo || true
fi

echo "=== Generating protobuf sources ==="
cd "$REPO_ROOT"
rm -f ClientServerChannelMessages.pb.h ClientServerChannelMessages.pb.cc
NS3_VERSION="$NS3_VERSION" ./premake5 gmake --generate-protobuf

echo "=== Checking premake-generated NR include paths ==="
grep -n "contrib/nr/helper" ns3-federate.make || true
grep -n "contrib/nr/model" ns3-federate.make || true
grep -n "contrib/nr/utils" ns3-federate.make || true
ls -l "$NS3_DIR/contrib/nr/helper/nr-helper.h"
ls -l "$NS3_DIR/contrib/nr/helper/cc-bwp-helper.h" || true

echo "=== Building ns3-federate ==="
NS3_VERSION="$NS3_VERSION" make config=debug clean
NS3_VERSION="$NS3_VERSION" make -j1 config=debug

if [[ -f "$REPO_ROOT/bin/Debug/ns3-federate" ]]; then
  cp -f "$REPO_ROOT/bin/Debug/ns3-federate" "$REPO_ROOT/ns3-federate"
fi

chmod +x "$REPO_ROOT/run.sh" "$REPO_ROOT/run_from_mosaic.sh" "$REPO_ROOT/run_manually.sh" "$REPO_ROOT/premake5"
mkdir -p "$REPO_ROOT/ns3config"
chmod 777 "$REPO_ROOT/ns3config"

echo "=== install_dependencies.sh complete ==="
echo "=== NS3_FLAVOR=${NS3_FLAVOR} NS3_VERSION=${NS3_VERSION} ==="
