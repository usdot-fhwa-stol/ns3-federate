#!/usr/bin/env bash
#
# Copyright (C) 2026 LEIDOS.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/home/mosaic/bin/fed/ns3}"
DSRC_NS3_VERSION="${DSRC_NS3_VERSION:-3.28}"
DSRC_FEDERATE_REPO="${DSRC_FEDERATE_REPO:-https://github.com/mosaic-addons/ns3-federate.git}"
DSRC_FEDERATE_REF="${DSRC_FEDERATE_REF:-21.0}"
PREMAKE_URL="${PREMAKE_URL:-https://github.com/premake/premake-core/releases/download/v5.0.0-alpha12/premake-5.0.0-alpha12-linux.tar.gz}"
PREMAKE_AUTOCONF_URL="${PREMAKE_AUTOCONF_URL:-https://github.com/Blizzard/premake-autoconf/archive/master.zip}"

NS3_ARCHIVE="ns-allinone-${DSRC_NS3_VERSION}.tar.bz2"
NS3_URL="${NS3_URL:-https://www.nsnam.org/releases/${NS3_ARCHIVE}}"
NS3_ALLINONE_DIR="${REPO_ROOT}/ns-allinone-${DSRC_NS3_VERSION}"
NS3_DIR="${NS3_ALLINONE_DIR}/ns-${DSRC_NS3_VERSION}"
FEDERATE_DIR="${REPO_ROOT}/dsrc-federate"
DEPLOY_DIR="${REPO_ROOT}/ns3-deployed"

echo "=== Downloading ns-3 DSRC base ${DSRC_NS3_VERSION} ==="
wget -q "$NS3_URL" -O "${REPO_ROOT}/${NS3_ARCHIVE}"
tar -xf "${REPO_ROOT}/${NS3_ARCHIVE}" -C "$REPO_ROOT"

echo "=== Cloning DSRC federate ${DSRC_FEDERATE_REF} ==="
git clone --depth 1 --branch "$DSRC_FEDERATE_REF" "$DSRC_FEDERATE_REPO" "$FEDERATE_DIR"

echo "=== Preparing premake ==="
wget -q "$PREMAKE_URL" -O /tmp/ns3-federate-premake.tar.gz
tar -xzf /tmp/ns3-federate-premake.tar.gz -C "$FEDERATE_DIR"
wget -q "$PREMAKE_AUTOCONF_URL" -O /tmp/ns3-federate-premake-autoconf.zip
unzip -q /tmp/ns3-federate-premake-autoconf.zip -d /tmp/ns3-federate-premake-autoconf
cp /tmp/ns3-federate-premake-autoconf/premake-autoconf-master/*.lua "$FEDERATE_DIR"

echo "=== Building ns-3 ${DSRC_NS3_VERSION} for DSRC/WAVE ==="
cd "$NS3_ALLINONE_DIR"
CXXFLAGS="-Wno-error" python3.6 ./build.py --disable-netanim
rm -rf /usr/include/ns3
cp -a "${NS3_DIR}/build/ns3" /usr/include/ns3

echo "=== Building mosaic-addons DSRC federate ==="
cd "$FEDERATE_DIR"
mv src/ClientServerChannel.h src/ClientServerChannel.cc .
rm -f src/ClientServerChannelMessages.pb.h src/ClientServerChannelMessages.pb.cc

sed -i \
  -e 's|/usr/local|.|' \
  -e "s|\"/usr/include\"|\"../ns-allinone-${DSRC_NS3_VERSION}/ns-${DSRC_NS3_VERSION}/build\"|" \
  -e "s|\"/usr/lib\"|\"../ns-allinone-${DSRC_NS3_VERSION}/ns-${DSRC_NS3_VERSION}/build\"|" \
  premake5.lua

./premake5 gmake --generate-protobuf --install
make config=debug clean
make -j1 config=debug

echo "=== Deploying DSRC runtime ==="
cp run.sh "${REPO_ROOT}/run.sh"
chmod +x "${REPO_ROOT}/run.sh"

mkdir -p "${DEPLOY_DIR}/build/scratch" "${DEPLOY_DIR}/scratch"
find "${NS3_DIR}/build" -name '*.so' -exec cp '{}' "${DEPLOY_DIR}/build/" ';'
cp "${FEDERATE_DIR}/bin/ns3-federate" "${DEPLOY_DIR}/build/scratch/mosaic_starter"

rm -rf "$NS3_DIR"
mv "$DEPLOY_DIR" "$NS3_DIR"

rm -rf \
  "$FEDERATE_DIR" \
  "${REPO_ROOT:?}/${NS3_ARCHIVE}" \
  /tmp/ns3-federate-premake.tar.gz \
  /tmp/ns3-federate-premake-autoconf.zip \
  /tmp/ns3-federate-premake-autoconf

echo "=== DSRC installation complete ==="
echo "=== DSRC_NS3_VERSION=${DSRC_NS3_VERSION} DSRC_FEDERATE_REF=${DSRC_FEDERATE_REF} ==="
