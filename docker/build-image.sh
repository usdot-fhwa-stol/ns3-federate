#!/bin/bash
#  Copyright (C) 2026 LEIDOS.
#
#  Licensed under the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License. You may obtain a copy of
#  the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#  License for the specific language governing permissions and limitations under
#  the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
IMAGE="$(basename "$REPO_ROOT")"
USERNAME=usdotfhwastol
RADIO_TECH=dsrc
COMPONENT_VERSION_STRING=""
SYSTEM_RELEASE=false
PUSH=false

usage() {
  cat <<'EOF'
Usage: ./docker/build-image.sh [options] [dsrc|5g-nr]

Build an ns3-federate image. DSRC is selected when no radio technology is given.

Options:
  -r, --radio-tech <dsrc|5g-nr>  Radio implementation to build (default: dsrc)
  -v, --version <version>        Override the component version
      --system-release           Add a CARMA System release tag
  -p, --push                     Push every generated tag
  -d, --develop                  Use the development Docker organization and tag
  -h, --help                     Show this help text
EOF
}

require_value() {
  local option="$1"
  local value="${2:-}"
  if [[ -z "$value" || "$value" == -* ]]; then
    echo "Option $option requires a value." >&2
    usage >&2
    exit 2
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--radio-tech|--technology)
      require_value "$1" "${2:-}"
      RADIO_TECH="$2"
      shift 2
      ;;
    -v|--version)
      require_value "$1" "${2:-}"
      COMPONENT_VERSION_STRING="$2"
      shift 2
      ;;
    --system-release)
      SYSTEM_RELEASE=true
      shift
      ;;
    -p|--push)
      PUSH=true
      shift
      ;;
    -d|--develop)
      USERNAME=usdotfhwastoldev
      COMPONENT_VERSION_STRING=develop
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    dsrc|DSRC|nr|NR|5g-nr|5G-NR|5g_nr|5G_NR)
      RADIO_TECH="$1"
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${RADIO_TECH,,}" in
  dsrc)
    RADIO_TECH=dsrc
    DOCKERFILE="$REPO_ROOT/Dockerfile.dsrc"
    ;;
  nr|5g-nr|5g_nr)
    RADIO_TECH=5g-nr
    DOCKERFILE="$REPO_ROOT/Dockerfile.nr"
    ;;
  *)
    echo "Unsupported radio technology: $RADIO_TECH" >&2
    echo "Supported values are dsrc and 5g-nr." >&2
    exit 2
    ;;
esac

if [[ -z "$COMPONENT_VERSION_STRING" ]]; then
  COMPONENT_VERSION_STRING="$("$SCRIPT_DIR/get-component-version.sh")"
fi

IMAGE_REPOSITORY="$USERNAME/$IMAGE"
PRIMARY_TAG="$COMPONENT_VERSION_STRING-$RADIO_TECH"
PRIMARY_IMAGE="$IMAGE_REPOSITORY:$PRIMARY_TAG"

echo ""
echo "##### $IMAGE Docker Image Build Script #####"
echo ""
echo "Radio technology: $RADIO_TECH"
echo "Dockerfile: ${DOCKERFILE#"$REPO_ROOT/"}"
echo "Component version: $COMPONENT_VERSION_STRING"
echo "Primary image: $PRIMARY_IMAGE"

docker build \
  --file "$DOCKERFILE" \
  --no-cache \
  --tag "$PRIMARY_IMAGE" \
  --build-arg VERSION="$COMPONENT_VERSION_STRING" \
  --build-arg VCS_REF="$(git -C "$REPO_ROOT" rev-parse --short HEAD)" \
  --build-arg BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  "$REPO_ROOT"

TAGS=("$PRIMARY_IMAGE")

add_tag() {
  local tag="$1"
  local image="$IMAGE_REPOSITORY:$tag"
  docker tag "$PRIMARY_IMAGE" "$image"
  TAGS+=("$image")
  echo "Tagged $PRIMARY_IMAGE as $image"
}

add_tag "latest-$RADIO_TECH"

# DSRC is the backward-compatible default for unqualified image tags.
if [[ "$RADIO_TECH" == "dsrc" ]]; then
  add_tag "$COMPONENT_VERSION_STRING"
  add_tag latest
fi

if [[ "$SYSTEM_RELEASE" == true ]]; then
  SYSTEM_VERSION_STRING="$("$SCRIPT_DIR/get-system-version.sh")"
  add_tag "$SYSTEM_VERSION_STRING-$RADIO_TECH"
  if [[ "$RADIO_TECH" == "dsrc" ]]; then
    add_tag "$SYSTEM_VERSION_STRING"
  fi
fi

if [[ "$PUSH" == true ]]; then
  for tag in "${TAGS[@]}"; do
    docker push "$tag"
  done
fi

echo ""
echo "Generated image tags:"
printf '  %s\n' "${TAGS[@]}"
echo "##### $IMAGE Docker Image Build Done! #####"
