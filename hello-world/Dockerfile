ARG ARCH=armv7hf
ARG VERSION=12.11.0
ARG UBUNTU_VERSION=24.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk

FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION} AS builder

# Building the ACAP application
COPY ./app /opt/app/
WORKDIR /opt/app
RUN . /opt/axis/acapsdk/environment-setup* && acap-build ./

# Export the build output with no unnecessary layer left. This copies the whole
# app directory, so `build/` ends up holding the sources and `package.conf`
# beside the `.eap` — glob `build/*.eap` rather than assuming it is alone there.
FROM scratch AS export
COPY --from=builder /opt/app/* /
