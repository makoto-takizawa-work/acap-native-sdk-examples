ARG ARCH=aarch64
ARG VERSION=12.11.0
ARG REPO=axisecp

FROM ${REPO}/acap-native-sdk:${VERSION}-${ARCH} AS builder
COPY app /opt/app/
WORKDIR /opt/app
RUN . /opt/axis/acapsdk/environment-setup* && acap-build ./

# Export only the build output (the .eap and the files acap-build leaves beside it)
FROM scratch AS export
COPY --from=builder /opt/app/* /
