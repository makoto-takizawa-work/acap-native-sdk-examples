ARG ARCH=aarch64
ARG VERSION=12.11.0
ARG REPO=axisecp

FROM ${REPO}/acap-native-sdk:${VERSION}-${ARCH} AS builder
COPY app /opt/app/
WORKDIR /opt/app
RUN . /opt/axis/acapsdk/environment-setup* && acap-build ./ -a hello_world_aarch64 -a hello_world_armv7l

# Export the build output with no unnecessary layer left. This copies the whole
# app directory, so `build/` ends up holding the sources and `package.conf`
# beside the `.eap` — glob `build/*.eap` rather than assuming it is alone there.
FROM scratch AS export
COPY --from=builder /opt/app/* /
