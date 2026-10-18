ARG ARCH=aarch64
ARG VERSION=12.11.0
ARG REPO=axisecp

FROM ${REPO}/acap-native-sdk:${VERSION}-${ARCH} AS builder
ARG VDO_FORMAT=h264

# Building the ACAP application
COPY ./app /opt/app/
WORKDIR /opt/app
RUN cp /opt/app/manifest.json.${VDO_FORMAT} manifest.json && \
    . /opt/axis/acapsdk/environment-setup* && \
    acap-build ./

# Export the build output with no unnecessary layer left
FROM scratch AS export
COPY --from=builder /opt/app/* /
