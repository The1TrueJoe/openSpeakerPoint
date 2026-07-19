# Buildroot build container.
#
# Provides a reproducible build environment and builds firmware during
# `docker build`, then exports only final images.
#
# Usage (from repo root):
#   docker build --target artifacts --output type=local,dest=./output .
FROM debian:bookworm-slim AS build-env

ARG BUILDROOT_REPO=https://github.com/buildroot/buildroot
ARG BUILDROOT_REF=master

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        bc \
        bison \
        flex \
        git \
        rsync \
        unzip \
        wget \
        cpio \
        file \
        python3 \
        python3-dev \
        libncurses-dev \
        libssl-dev \
        ca-certificates \
        perl \
        patch \
        gcc \
        g++ \
        make \
        sed \
        cvs \
        subversion \
        mercurial \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "$BUILDROOT_REF" "$BUILDROOT_REPO" /opt/buildroot

# Buildroot refuses to run as root by default; create an unprivileged user
# so `make` behaves the same as on a normal dev machine.
RUN useradd -m -s /bin/bash builder \
    && chown -R builder:builder /opt/buildroot

ENV BUILDROOT_DIR=/opt/buildroot
USER builder

WORKDIR /src
COPY --chown=builder:builder . /src

RUN bash ./build.sh

FROM scratch AS artifacts

COPY --from=build-env /src/output/images/ /images/
