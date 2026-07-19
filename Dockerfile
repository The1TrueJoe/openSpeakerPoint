# Buildroot build container.
#
# Provides a reproducible build environment for anyone (Mac, CI runner,
# whatever) without needing to hand-install Buildroot's host dependencies.
#
# Usage (from repo root):
#   docker build -t speakerpoint-build docker/
#   docker run --rm -it -v "$PWD":/src -w /src speakerpoint-build ./build.sh
#
# The dl/ and output/ dirs are created inside the mounted repo (see
# .gitignore) so the download cache and build output persist across
# container runs.
FROM debian:bookworm-slim

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

# Buildroot refuses to run as root by default; create an unprivileged user
# so `make` behaves the same as on a normal dev machine.
RUN useradd -m -s /bin/bash builder
USER builder

WORKDIR /src
