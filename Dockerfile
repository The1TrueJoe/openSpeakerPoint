# Buildroot build container.
#
# Usage (from repo root):
#   docker build --target artifacts --output type=local,dest=./output .
FROM debian:bookworm-slim AS build-env

ARG BUILDROOT_REPO=https://github.com/buildroot/buildroot
# Pinned to a commit, so a rebuild is reproducible. A branch, tag or full SHA
# all work. Not 2026.02.3: kernels built with its gcc 14 toolchain hang at the
# RedBoot handoff on this ARM920T, before printing anything; this master
# commit's gcc 15 toolchain boots (and brings mosquitto 2.1.2).
ARG BUILDROOT_REF=c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b
ARG BR2_JLEVEL=
ARG TOPLEVEL_JOBS=
ARG BUILD_TARGETS=all
ARG FORCE_CLEAN=

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
        nodejs \
        npm \
    && rm -rf /var/lib/apt/lists/*

RUN git init -q /opt/buildroot \
    && git -C /opt/buildroot fetch -q --depth 1 "$BUILDROOT_REPO" "$BUILDROOT_REF" \
    && git -C /opt/buildroot checkout -q FETCH_HEAD

# Buildroot refuses to run as root by default; create an unprivileged user
# so `make` behaves the same as on a normal dev machine.
RUN useradd -m -s /bin/bash builder \
    && chown -R builder:builder /opt/buildroot

ENV BUILDROOT_DIR=/opt/buildroot
USER builder

WORKDIR /src
COPY --chown=builder:builder . /src

RUN --mount=type=cache,target=/src/dl,uid=1000,gid=1000 \
    --mount=type=cache,target=/src/output,uid=1000,gid=1000 \
    BR2_JLEVEL="$BR2_JLEVEL" TOPLEVEL_JOBS="$TOPLEVEL_JOBS" FORCE_CLEAN="$FORCE_CLEAN" bash ./build.sh $BUILD_TARGETS \
    && rm -rf /tmp/artifacts \
    && mkdir -p /tmp/artifacts \
    && cp -a /src/output/images/. /tmp/artifacts/

FROM scratch AS artifacts

COPY --from=build-env /tmp/artifacts/ /images/
