#!/bin/bash
# Copyright (C) 2018-2026 LEIDOS.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

NS3_VERSION="3.36.1"
NS3_TARBALL="ns-allinone-${NS3_VERSION}.tar.bz2"
NS3_URL="https://www.nsnam.org/releases/${NS3_TARBALL}"
PREMAKE_URL="https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz"
REPO_ROOT="/opt/ns3-federate"

echo "=== Updating apt and installing ns-3 federate build dependencies ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  gcc \
  g++ \
  make \
  pkg-config \
  lbzip2 \
  ca-certificates \
  curl \
  git \
  libprotobuf-dev \
  libsqlite3-dev \
  libxml2-dev \
  patch \
  protobuf-compiler \
  python3 \
  rsync \
  unzip \
  wget \
  cmake
rm -rf /var/lib/apt/lists/*

cd "$REPO_ROOT"

echo "=== Downloading premake5 ==="
if [[ ! -x "$REPO_ROOT/premake5" ]]; then
  wget -q "$PREMAKE_URL" -O /tmp/premake.tar.gz
  tar -xzf /tmp/premake.tar.gz -C "$REPO_ROOT"
  rm -f /tmp/premake.tar.gz
fi

echo "=== Downloading ns-3 ${NS3_VERSION} ==="
if [[ ! -f "$REPO_ROOT/${NS3_TARBALL}" ]]; then
  wget -q "$NS3_URL" -O "$REPO_ROOT/${NS3_TARBALL}"
fi

if [[ ! -d "$REPO_ROOT/ns-allinone-${NS3_VERSION}" ]]; then
  echo "=== Extracting ns-3 ${NS3_VERSION} ==="
  tar -xf "$REPO_ROOT/${NS3_TARBALL}" -C "$REPO_ROOT"
fi

cd "$REPO_ROOT/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}"
if [[ ! -f ".ns3_federate_patch_applied" ]]; then
  echo "=== Applying ns-3 patch ==="
  patch --strip=1 --input="$REPO_ROOT/patches/ns3-lte.patch"
  touch .ns3_federate_patch_applied
fi

echo "=== Building ns-3 ${NS3_VERSION} ==="
cd "$REPO_ROOT/ns-allinone-${NS3_VERSION}"
CXXFLAGS="-Wno-error" python3 ./build.py --disable-netanim

echo "=== Generating protobuf sources ==="
cd "$REPO_ROOT"
rm -f ClientServerChannelMessages.pb.h ClientServerChannelMessages.pb.cc
./premake5 gmake --generate-protobuf

echo "=== Building ns3-federate ==="
make config=debug clean
make -j1 config=debug

if [[ -f "$REPO_ROOT/bin/Debug/ns3-federate" ]]; then
  cp -f "$REPO_ROOT/bin/Debug/ns3-federate" "$REPO_ROOT/ns3-federate"
fi

cp -f "$REPO_ROOT/run_from_mosaic.sh" "$REPO_ROOT/run.sh"
chmod +x "$REPO_ROOT/run.sh" "$REPO_ROOT/ns3-federate" "$REPO_ROOT/premake5"
mkdir -p "$REPO_ROOT/ns3config"
chmod 777 "$REPO_ROOT/ns3config"

echo "=== install_dependencies.sh complete ==="
