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

FROM ubuntu:jammy

ARG VERSION=unknown
ARG VCS_REF=unknown
ARG BUILD_DATE=unknown
ARG NS3_FLAVOR=v2x
ARG NS3_VERSION=3.42
ARG V2X_NS3_VERSION=3.42
ARG V2X_NS3_REF=ns-3-dev-v2x-v1.1
ARG V2X_NR_REF=v2x-1.1
ARG LEGACY_NS3_VERSION=3.38
ARG LEGACY_NR_BRANCH=5g-lena-v2.4.y

LABEL org.opencontainers.image.title="ns3-federate" \
      org.opencontainers.image.description="Docker image containing the MOSAIC adapted ns-3 federate" \
      org.opencontainers.image.version="$VERSION" \
      org.opencontainers.image.revision="$VCS_REF" \
      org.opencontainers.image.created="$BUILD_DATE" \
      org.opencontainers.image.vendor="LEIDOS"

WORKDIR /opt/ns3-federate

COPY . /opt/ns3-federate

RUN chmod +x docker/*.sh run.sh run_from_mosaic.sh run_manually.sh && \
    NS3_FLAVOR="$NS3_FLAVOR" \
    NS3_VERSION="$NS3_VERSION" \
    V2X_NS3_VERSION="$V2X_NS3_VERSION" \
    V2X_NS3_REF="$V2X_NS3_REF" \
    V2X_NR_REF="$V2X_NR_REF" \
    LEGACY_NS3_VERSION="$LEGACY_NS3_VERSION" \
    LEGACY_NR_BRANCH="$LEGACY_NR_BRANCH" \
    ./docker/install_dependencies.sh

ENV NS3_FLAVOR=${NS3_FLAVOR}
ENV NS3_VERSION=${NS3_VERSION}
ENV LD_LIBRARY_PATH=/opt/ns3-federate/ns-allinone-${V2X_NS3_VERSION}/ns-${V2X_NS3_VERSION}/build/lib

VOLUME ["/opt/ns3-federate/ns3config"]

EXPOSE 40001 40002

ENTRYPOINT ["./run.sh", "40001", "40002"]
