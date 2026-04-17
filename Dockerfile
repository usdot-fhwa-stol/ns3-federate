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

LABEL org.opencontainers.image.title="ns3-federate" \
      org.opencontainers.image.description="Docker image containing the MOSAIC adapted ns-3 federate" \
      org.opencontainers.image.version="$VERSION" \
      org.opencontainers.image.revision="$VCS_REF" \
      org.opencontainers.image.created="$BUILD_DATE" \
      org.opencontainers.image.vendor="LEIDOS"

WORKDIR /opt/ns3-federate

COPY . /opt/ns3-federate

RUN chmod +x docker/*.sh run.sh run_from_mosaic.sh run_manually.sh && \
    ./docker/install_dependencies.sh

ENV LD_LIBRARY_PATH=/opt/ns3-federate/ns-allinone-3.38/ns-3.38/build/lib

VOLUME ["/opt/ns3-federate/ns3config"]

EXPOSE 40001 40002

ENTRYPOINT ["./run.sh", "40001", "40002"]